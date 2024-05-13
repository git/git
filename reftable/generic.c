/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "constants.h"
#include "record.h"
#include "generic.h"
#include "reftable-iterator.h"
#include "reftable-generic.h"

void table_init_iter(struct reftable_table *tab,
		     struct reftable_iterator *it,
		     uint8_t typ)
{

	tab->ops->init_iter(tab->table_arg, it, typ);
}

void reftable_table_init_ref_iter(struct reftable_table *tab,
				  struct reftable_iterator *it)
{
	table_init_iter(tab, it, BLOCK_TYPE_REF);
}

void reftable_table_init_log_iter(struct reftable_table *tab,
				  struct reftable_iterator *it)
{
	table_init_iter(tab, it, BLOCK_TYPE_LOG);
}

int reftable_iterator_seek_ref(struct reftable_iterator *it,
			       const char *name)
{
	struct reftable_record want = {
		.type = BLOCK_TYPE_REF,
		.u.ref = {
			.refname = (char *)name,
		},
	};
	return it->ops->seek(it->iter_arg, &want);
}

int reftable_iterator_seek_log_at(struct reftable_iterator *it,
				  const char *name, uint64_t update_index)
{
	struct reftable_record want = {
		.type = BLOCK_TYPE_LOG,
		.u.log = {
			.refname = (char *)name,
			.update_index = update_index,
		},
	};
	return it->ops->seek(it->iter_arg, &want);
}

int reftable_iterator_seek_log(struct reftable_iterator *it,
			       const char *name)
{
	return reftable_iterator_seek_log_at(it, name, ~((uint64_t) 0));
}

int reftable_table_read_ref(struct reftable_table *tab, const char *name,
			    struct reftable_ref_record *ref)
{
	struct reftable_iterator it = { NULL };
	int err;

	reftable_table_init_ref_iter(tab, &it);

	err = reftable_iterator_seek_ref(&it, name);
	if (err)
		goto done;

	err = reftable_iterator_next_ref(&it, ref);
	if (err)
		goto done;

	if (strcmp(ref->refname, name) ||
	    reftable_ref_record_is_deletion(ref)) {
		reftable_ref_record_release(ref);
		err = 1;
		goto done;
	}

done:
	reftable_iterator_destroy(&it);
	return err;
}

int reftable_table_print(struct reftable_table *tab) {
	struct reftable_iterator it = { NULL };
	struct reftable_ref_record ref = { NULL };
	struct reftable_log_record log = { NULL };
	uint32_t hash_id = reftable_table_hash_id(tab);
	int err;

	reftable_table_init_ref_iter(tab, &it);

	err = reftable_iterator_seek_ref(&it, "");
	if (err < 0)
		return err;

	while (1) {
		err = reftable_iterator_next_ref(&it, &ref);
		if (err > 0) {
			break;
		}
		if (err < 0) {
			return err;
		}
		reftable_ref_record_print(&ref, hash_id);
	}
	reftable_iterator_destroy(&it);
	reftable_ref_record_release(&ref);

	reftable_table_init_log_iter(tab, &it);

	err = reftable_iterator_seek_log(&it, "");
	if (err < 0)
		return err;

	while (1) {
		err = reftable_iterator_next_log(&it, &log);
		if (err > 0) {
			break;
		}
		if (err < 0) {
			return err;
		}
		reftable_log_record_print(&log, hash_id);
	}
	reftable_iterator_destroy(&it);
	reftable_log_record_release(&log);
	return 0;
}

uint64_t reftable_table_max_update_index(struct reftable_table *tab)
{
	return tab->ops->max_update_index(tab->table_arg);
}

uint64_t reftable_table_min_update_index(struct reftable_table *tab)
{
	return tab->ops->min_update_index(tab->table_arg);
}

uint32_t reftable_table_hash_id(struct reftable_table *tab)
{
	return tab->ops->hash_id(tab->table_arg);
}

void reftable_iterator_destroy(struct reftable_iterator *it)
{
	if (!it->ops) {
		return;
	}
	it->ops->close(it->iter_arg);
	it->ops = NULL;
	FREE_AND_NULL(it->iter_arg);
}

int reftable_iterator_next_ref(struct reftable_iterator *it,
			       struct reftable_ref_record *ref)
{
	struct reftable_record rec = {
		.type = BLOCK_TYPE_REF,
		.u = {
			.ref = *ref
		},
	};
	int err = iterator_next(it, &rec);
	*ref = rec.u.ref;
	return err;
}

int reftable_iterator_next_log(struct reftable_iterator *it,
			       struct reftable_log_record *log)
{
	struct reftable_record rec = {
		.type = BLOCK_TYPE_LOG,
		.u = {
			.log = *log,
		},
	};
	int err = iterator_next(it, &rec);
	*log = rec.u.log;
	return err;
}

int iterator_seek(struct reftable_iterator *it, struct reftable_record *want)
{
	return it->ops->seek(it->iter_arg, want);
}

int iterator_next(struct reftable_iterator *it, struct reftable_record *rec)
{
	return it->ops->next(it->iter_arg, rec);
}

static int empty_iterator_seek(void *arg, struct reftable_record *want)
{
	return 0;
}

static int empty_iterator_next(void *arg, struct reftable_record *rec)
{
	return 1;
}

static void empty_iterator_close(void *arg)
{
}

static struct reftable_iterator_vtable empty_vtable = {
	.seek = &empty_iterator_seek,
	.next = &empty_iterator_next,
	.close = &empty_iterator_close,
};

void iterator_set_empty(struct reftable_iterator *it)
{
	assert(!it->ops);
	it->iter_arg = NULL;
	it->ops = &empty_vtable;
}
