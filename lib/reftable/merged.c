/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#include "merged.h"

#include "constants.h"
#include "iter.h"
#include "pq.h"
#include "record.h"
#include "reftable-merged.h"
#include "reftable-error.h"
#include "system.h"
#include "table.h"

struct merged_subiter {
	struct reftable_iterator iter;
	struct reftable_record rec;
};

struct merged_iter {
	struct merged_subiter *subiters;
	struct merged_iter_pqueue pq;
	size_t subiters_len;
	int suppress_deletions;
	ssize_t advance_index;
};

static void merged_iter_close(void *p)
{
	struct merged_iter *mi = p;

	merged_iter_pqueue_release(&mi->pq);
	for (size_t i = 0; i < mi->subiters_len; i++) {
		reftable_iterator_destroy(&mi->subiters[i].iter);
		reftable_record_release(&mi->subiters[i].rec);
	}
	reftable_free(mi->subiters);
}

static int merged_iter_advance_subiter(struct merged_iter *mi, size_t idx)
{
	struct pq_entry e = {
		.index = idx,
		.rec = &mi->subiters[idx].rec,
	};
	int err;

	err = iterator_next(&mi->subiters[idx].iter, &mi->subiters[idx].rec);
	if (err)
		return err;

	err = merged_iter_pqueue_add(&mi->pq, &e);
	if (err)
		return err;

	return 0;
}

static int merged_iter_seek(struct merged_iter *mi, struct reftable_record *want)
{
	int err;

	mi->advance_index = -1;
	while (!merged_iter_pqueue_is_empty(mi->pq)) {
		err = merged_iter_pqueue_remove(&mi->pq, NULL);
		if (err < 0)
			return err;
	}

	for (size_t i = 0; i < mi->subiters_len; i++) {
		err = iterator_seek(&mi->subiters[i].iter, want);
		if (err < 0)
			return err;
		if (err > 0)
			continue;

		err = merged_iter_advance_subiter(mi, i);
		if (err < 0)
			return err;
	}

	return 0;
}

static int merged_iter_next_entry(struct merged_iter *mi,
				  struct reftable_record *rec)
{
	struct pq_entry entry = { 0 };
	int err = 0, empty;

	empty = merged_iter_pqueue_is_empty(mi->pq);

	if (mi->advance_index >= 0) {
		/*
		 * When there are no pqueue entries then we only have a single
		 * subiter left. There is no need to use the pqueue in that
		 * case anymore as we know that the subiter will return entries
		 * in the correct order already.
		 *
		 * While this may sound like a very specific edge case, it may
		 * happen more frequently than you think. Most repositories
		 * will end up having a single large base table that contains
		 * most of the refs. It's thus likely that we exhaust all
		 * subiters but the one from that base ref.
		 */
		if (empty)
			return iterator_next(&mi->subiters[mi->advance_index].iter,
					     rec);

		err = merged_iter_advance_subiter(mi, mi->advance_index);
		if (err < 0)
			return err;
		if (!err)
			empty = 0;
		mi->advance_index = -1;
	}

	if (empty)
		return 1;

	err = merged_iter_pqueue_remove(&mi->pq, &entry);
	if (err < 0)
		return err;

	/*
	  One can also use reftable as datacenter-local storage, where the ref
	  database is maintained in globally consistent database (eg.
	  CockroachDB or Spanner). In this scenario, replication delays together
	  with compaction may cause newer tables to contain older entries. In
	  such a deployment, the loop below must be changed to collect all
	  entries for the same key, and return new the newest one.
	*/
	while (!merged_iter_pqueue_is_empty(mi->pq)) {
		struct pq_entry top = merged_iter_pqueue_top(mi->pq);
		int cmp;

		err = reftable_record_cmp(top.rec, entry.rec, &cmp);
		if (err < 0)
			return err;
		if (cmp > 0)
			break;

		err = merged_iter_pqueue_remove(&mi->pq, NULL);
		if (err < 0)
			return err;

		err = merged_iter_advance_subiter(mi, top.index);
		if (err < 0)
			return err;
	}

	mi->advance_index = entry.index;
	REFTABLE_SWAP(*rec, *entry.rec);
	return 0;
}

static int merged_iter_seek_void(void *it, struct reftable_record *want)
{
	return merged_iter_seek(it, want);
}

static int merged_iter_next_void(void *p, struct reftable_record *rec)
{
	struct merged_iter *mi = p;
	while (1) {
		int err = merged_iter_next_entry(mi, rec);
		if (err)
			return err;
		if (mi->suppress_deletions && reftable_record_is_deletion(rec))
			continue;
		return 0;
	}
}

static struct reftable_iterator_vtable merged_iter_vtable = {
	.seek = merged_iter_seek_void,
	.next = &merged_iter_next_void,
	.close = &merged_iter_close,
};

static void iterator_from_merged_iter(struct reftable_iterator *it,
				      struct merged_iter *mi)
{
	assert(!it->ops);
	it->iter_arg = mi;
	it->ops = &merged_iter_vtable;
}

int reftable_merged_table_new(struct reftable_merged_table **dest,
			      struct reftable_table **tables, size_t n,
			      enum reftable_hash hash_id)
{
	struct reftable_merged_table *m = NULL;
	uint64_t last_max = 0;
	uint64_t first_min = 0;

	for (size_t i = 0; i < n; i++) {
		uint64_t min = reftable_table_min_update_index(tables[i]);
		uint64_t max = reftable_table_max_update_index(tables[i]);

		if (reftable_table_hash_id(tables[i]) != hash_id) {
			return REFTABLE_FORMAT_ERROR;
		}
		if (i == 0 || min < first_min) {
			first_min = min;
		}
		if (i == 0 || max > last_max) {
			last_max = max;
		}
	}

	REFTABLE_CALLOC_ARRAY(m, 1);
	if (!m)
		return REFTABLE_OUT_OF_MEMORY_ERROR;

	m->tables = tables;
	m->tables_len = n;
	m->min = first_min;
	m->max = last_max;
	m->hash_id = hash_id;
	*dest = m;
	return 0;
}

void reftable_merged_table_free(struct reftable_merged_table *mt)
{
	if (!mt)
		return;
	reftable_free(mt);
}

uint64_t
reftable_merged_table_max_update_index(struct reftable_merged_table *mt)
{
	return mt->max;
}

uint64_t
reftable_merged_table_min_update_index(struct reftable_merged_table *mt)
{
	return mt->min;
}

int merged_table_init_iter(struct reftable_merged_table *mt,
			   struct reftable_iterator *it,
			   uint8_t typ)
{
	struct merged_subiter *subiters = NULL;
	struct merged_iter *mi = NULL;
	int ret;

	if (mt->tables_len) {
		REFTABLE_CALLOC_ARRAY(subiters, mt->tables_len);
		if (!subiters) {
			ret = REFTABLE_OUT_OF_MEMORY_ERROR;
			goto out;
		}
	}

	for (size_t i = 0; i < mt->tables_len; i++) {
		ret = reftable_record_init(&subiters[i].rec, typ);
		if (ret < 0)
			goto out;

		ret = table_init_iter(mt->tables[i], &subiters[i].iter, typ);
		if (ret < 0)
			goto out;
	}

	REFTABLE_CALLOC_ARRAY(mi, 1);
	if (!mi) {
		ret = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto out;
	}
	mi->advance_index = -1;
	mi->suppress_deletions = mt->suppress_deletions;
	mi->subiters = subiters;
	mi->subiters_len = mt->tables_len;

	iterator_from_merged_iter(it, mi);
	ret = 0;

out:
	if (ret < 0) {
		for (size_t i = 0; subiters && i < mt->tables_len; i++) {
			reftable_iterator_destroy(&subiters[i].iter);
			reftable_record_release(&subiters[i].rec);
		}
		reftable_free(subiters);
		reftable_free(mi);
	}

	return ret;
}

int reftable_merged_table_init_ref_iterator(struct reftable_merged_table *mt,
					    struct reftable_iterator *it)
{
	return merged_table_init_iter(mt, it, REFTABLE_BLOCK_TYPE_REF);
}

int reftable_merged_table_init_log_iterator(struct reftable_merged_table *mt,
					    struct reftable_iterator *it)
{
	return merged_table_init_iter(mt, it, REFTABLE_BLOCK_TYPE_LOG);
}

enum reftable_hash reftable_merged_table_hash_id(struct reftable_merged_table *mt)
{
	return mt->hash_id;
}
