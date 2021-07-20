/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "basics.h"
#include "record.h"
#include "generic.h"
#include "reftable-iterator.h"
#include "reftable-generic.h"

int reftable_table_seek_ref(struct reftable_table *tab,
			    struct reftable_iterator *it, const char *name)
{
	struct reftable_ref_record ref = {
		.refname = (char *)name,
	};
	struct reftable_record rec = { NULL };
	reftable_record_from_ref(&rec, &ref);
	return tab->ops->seek_record(tab->table_arg, it, &rec);
}

int reftable_table_seek_log(struct reftable_table *tab,
			    struct reftable_iterator *it, const char *name)
{
	struct reftable_log_record log = {
		.refname = (char *)name,
		.update_index = ~((uint64_t)0),
	};
	struct reftable_record rec = { NULL };
	reftable_record_from_log(&rec, &log);
	return tab->ops->seek_record(tab->table_arg, it, &rec);
}

int reftable_table_read_ref(struct reftable_table *tab, const char *name,
			    struct reftable_ref_record *ref)
{
	struct reftable_iterator it = { NULL };
	int err = reftable_table_seek_ref(tab, &it, name);
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
	int err = reftable_table_seek_ref(tab, &it, "");
	if (err < 0) {
		return err;
	}

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

	err = reftable_table_seek_log(tab, &it, "");
	if (err < 0) {
		return err;
	}
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
	struct reftable_record rec = { NULL };
	reftable_record_from_ref(&rec, ref);
	return iterator_next(it, &rec);
}

int reftable_iterator_next_log(struct reftable_iterator *it,
			       struct reftable_log_record *log)
{
	struct reftable_record rec = { NULL };
	reftable_record_from_log(&rec, log);
	return iterator_next(it, &rec);
}

int iterator_next(struct reftable_iterator *it, struct reftable_record *rec)
{
	return it->ops->next(it->iter_arg, rec);
}

static int empty_iterator_next(void *arg, struct reftable_record *rec)
{
	return 1;
}

static void empty_iterator_close(void *arg)
{
}

static struct reftable_iterator_vtable empty_vtable = {
	.next = &empty_iterator_next,
	.close = &empty_iterator_close,
};

void iterator_set_empty(struct reftable_iterator *it)
{
	assert(!it->ops);
	it->iter_arg = NULL;
	it->ops = &empty_vtable;
}
