/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "merged.h"

#include "constants.h"
#include "iter.h"
#include "pq.h"
#include "record.h"
#include "generic.h"
#include "reftable-merged.h"
#include "reftable-error.h"
#include "system.h"

struct merged_subiter {
	struct reftable_iterator iter;
	struct reftable_record rec;
};

struct merged_iter {
	struct merged_subiter *subiters;
	struct merged_iter_pqueue pq;
	uint32_t hash_id;
	size_t stack_len;
	uint8_t typ;
	int suppress_deletions;
	ssize_t advance_index;
};

static int merged_iter_init(struct merged_iter *mi)
{
	for (size_t i = 0; i < mi->stack_len; i++) {
		struct pq_entry e = {
			.index = i,
			.rec = &mi->subiters[i].rec,
		};
		int err;

		reftable_record_init(&mi->subiters[i].rec, mi->typ);
		err = iterator_next(&mi->subiters[i].iter,
				    &mi->subiters[i].rec);
		if (err < 0)
			return err;
		if (err > 0)
			continue;

		merged_iter_pqueue_add(&mi->pq, &e);
	}

	return 0;
}

static void merged_iter_close(void *p)
{
	struct merged_iter *mi = p;

	merged_iter_pqueue_release(&mi->pq);
	for (size_t i = 0; i < mi->stack_len; i++) {
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

	merged_iter_pqueue_add(&mi->pq, &e);
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

	entry = merged_iter_pqueue_remove(&mi->pq);

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

		cmp = reftable_record_cmp(top.rec, entry.rec);
		if (cmp > 0)
			break;

		merged_iter_pqueue_remove(&mi->pq);
		err = merged_iter_advance_subiter(mi, top.index);
		if (err < 0)
			return err;
	}

	mi->advance_index = entry.index;
	SWAP(*rec, *entry.rec);
	return 0;
}

static int merged_iter_next(struct merged_iter *mi, struct reftable_record *rec)
{
	while (1) {
		int err = merged_iter_next_entry(mi, rec);
		if (err == 0 && mi->suppress_deletions &&
		    reftable_record_is_deletion(rec)) {
			continue;
		}

		return err;
	}
}

static int merged_iter_next_void(void *p, struct reftable_record *rec)
{
	struct merged_iter *mi = p;
	if (merged_iter_pqueue_is_empty(mi->pq) && mi->advance_index < 0)
		return 1;
	return merged_iter_next(mi, rec);
}

static struct reftable_iterator_vtable merged_iter_vtable = {
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

int reftable_new_merged_table(struct reftable_merged_table **dest,
			      struct reftable_table *stack, size_t n,
			      uint32_t hash_id)
{
	struct reftable_merged_table *m = NULL;
	uint64_t last_max = 0;
	uint64_t first_min = 0;

	for (size_t i = 0; i < n; i++) {
		uint64_t min = reftable_table_min_update_index(&stack[i]);
		uint64_t max = reftable_table_max_update_index(&stack[i]);

		if (reftable_table_hash_id(&stack[i]) != hash_id) {
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
	m->stack = stack;
	m->stack_len = n;
	m->min = first_min;
	m->max = last_max;
	m->hash_id = hash_id;
	*dest = m;
	return 0;
}

/* clears the list of subtable, without affecting the readers themselves. */
void merged_table_release(struct reftable_merged_table *mt)
{
	FREE_AND_NULL(mt->stack);
	mt->stack_len = 0;
}

void reftable_merged_table_free(struct reftable_merged_table *mt)
{
	if (!mt) {
		return;
	}
	merged_table_release(mt);
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

static int reftable_table_seek_record(struct reftable_table *tab,
				      struct reftable_iterator *it,
				      struct reftable_record *rec)
{
	return tab->ops->seek_record(tab->table_arg, it, rec);
}

static int merged_table_seek_record(struct reftable_merged_table *mt,
				    struct reftable_iterator *it,
				    struct reftable_record *rec)
{
	struct merged_iter merged = {
		.typ = reftable_record_type(rec),
		.hash_id = mt->hash_id,
		.suppress_deletions = mt->suppress_deletions,
		.advance_index = -1,
	};
	struct merged_iter *p;
	int err;

	REFTABLE_CALLOC_ARRAY(merged.subiters, mt->stack_len);
	for (size_t i = 0; i < mt->stack_len; i++) {
		err = reftable_table_seek_record(&mt->stack[i],
						 &merged.subiters[merged.stack_len].iter, rec);
		if (err < 0)
			goto out;
		if (!err)
			merged.stack_len++;
	}

	err = merged_iter_init(&merged);
	if (err < 0)
		goto out;

	p = reftable_malloc(sizeof(struct merged_iter));
	*p = merged;
	iterator_from_merged_iter(it, p);

out:
	if (err < 0)
		merged_iter_close(&merged);
	return err;
}

int reftable_merged_table_seek_ref(struct reftable_merged_table *mt,
				   struct reftable_iterator *it,
				   const char *name)
{
	struct reftable_record rec = {
		.type = BLOCK_TYPE_REF,
		.u.ref = {
			.refname = (char *)name,
		},
	};
	return merged_table_seek_record(mt, it, &rec);
}

int reftable_merged_table_seek_log_at(struct reftable_merged_table *mt,
				      struct reftable_iterator *it,
				      const char *name, uint64_t update_index)
{
	struct reftable_record rec = { .type = BLOCK_TYPE_LOG,
				       .u.log = {
					       .refname = (char *)name,
					       .update_index = update_index,
				       } };
	return merged_table_seek_record(mt, it, &rec);
}

int reftable_merged_table_seek_log(struct reftable_merged_table *mt,
				   struct reftable_iterator *it,
				   const char *name)
{
	uint64_t max = ~((uint64_t)0);
	return reftable_merged_table_seek_log_at(mt, it, name, max);
}

uint32_t reftable_merged_table_hash_id(struct reftable_merged_table *mt)
{
	return mt->hash_id;
}

static int reftable_merged_table_seek_void(void *tab,
					   struct reftable_iterator *it,
					   struct reftable_record *rec)
{
	return merged_table_seek_record(tab, it, rec);
}

static uint32_t reftable_merged_table_hash_id_void(void *tab)
{
	return reftable_merged_table_hash_id(tab);
}

static uint64_t reftable_merged_table_min_update_index_void(void *tab)
{
	return reftable_merged_table_min_update_index(tab);
}

static uint64_t reftable_merged_table_max_update_index_void(void *tab)
{
	return reftable_merged_table_max_update_index(tab);
}

static struct reftable_table_vtable merged_table_vtable = {
	.seek_record = reftable_merged_table_seek_void,
	.hash_id = reftable_merged_table_hash_id_void,
	.min_update_index = reftable_merged_table_min_update_index_void,
	.max_update_index = reftable_merged_table_max_update_index_void,
};

void reftable_table_from_merged_table(struct reftable_table *tab,
				      struct reftable_merged_table *merged)
{
	assert(!tab->ops);
	tab->ops = &merged_table_vtable;
	tab->table_arg = merged;
}
