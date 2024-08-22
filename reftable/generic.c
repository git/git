/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "constants.h"
#include "record.h"
#include "generic.h"
#include "iter.h"
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
