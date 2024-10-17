/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "iter.h"

#include "system.h"

#include "block.h"
#include "constants.h"
#include "reader.h"
#include "reftable-error.h"

int iterator_seek(struct reftable_iterator *it, struct reftable_record *want)
{
	return it->ops->seek(it->iter_arg, want);
}

int iterator_next(struct reftable_iterator *it, struct reftable_record *rec)
{
	return it->ops->next(it->iter_arg, rec);
}

static int empty_iterator_seek(void *arg UNUSED, struct reftable_record *want UNUSED)
{
	return 0;
}

static int empty_iterator_next(void *arg UNUSED, struct reftable_record *rec UNUSED)
{
	return 1;
}

static void empty_iterator_close(void *arg UNUSED)
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

static void filtering_ref_iterator_close(void *iter_arg)
{
	struct filtering_ref_iterator *fri = iter_arg;
	reftable_buf_release(&fri->oid);
	reftable_iterator_destroy(&fri->it);
}

static int filtering_ref_iterator_seek(void *iter_arg,
				       struct reftable_record *want)
{
	struct filtering_ref_iterator *fri = iter_arg;
	return iterator_seek(&fri->it, want);
}

static int filtering_ref_iterator_next(void *iter_arg,
				       struct reftable_record *rec)
{
	struct filtering_ref_iterator *fri = iter_arg;
	struct reftable_ref_record *ref = &rec->u.ref;
	int err = 0;
	while (1) {
		err = reftable_iterator_next_ref(&fri->it, ref);
		if (err != 0) {
			break;
		}

		if (ref->value_type == REFTABLE_REF_VAL2 &&
		    (!memcmp(fri->oid.buf, ref->value.val2.target_value,
			     fri->oid.len) ||
		     !memcmp(fri->oid.buf, ref->value.val2.value,
			     fri->oid.len)))
			return 0;

		if (ref->value_type == REFTABLE_REF_VAL1 &&
		    !memcmp(fri->oid.buf, ref->value.val1, fri->oid.len)) {
			return 0;
		}
	}

	reftable_ref_record_release(ref);
	return err;
}

static struct reftable_iterator_vtable filtering_ref_iterator_vtable = {
	.seek = &filtering_ref_iterator_seek,
	.next = &filtering_ref_iterator_next,
	.close = &filtering_ref_iterator_close,
};

void iterator_from_filtering_ref_iterator(struct reftable_iterator *it,
					  struct filtering_ref_iterator *fri)
{
	assert(!it->ops);
	it->iter_arg = fri;
	it->ops = &filtering_ref_iterator_vtable;
}

static void indexed_table_ref_iter_close(void *p)
{
	struct indexed_table_ref_iter *it = p;
	block_iter_close(&it->cur);
	reftable_block_done(&it->block_reader.block);
	reftable_free(it->offsets);
	reftable_buf_release(&it->oid);
}

static int indexed_table_ref_iter_next_block(struct indexed_table_ref_iter *it)
{
	uint64_t off;
	int err = 0;
	if (it->offset_idx == it->offset_len) {
		it->is_finished = 1;
		return 1;
	}

	reftable_block_done(&it->block_reader.block);

	off = it->offsets[it->offset_idx++];
	err = reader_init_block_reader(it->r, &it->block_reader, off,
				       BLOCK_TYPE_REF);
	if (err < 0) {
		return err;
	}
	if (err > 0) {
		/* indexed block does not exist. */
		return REFTABLE_FORMAT_ERROR;
	}
	block_iter_seek_start(&it->cur, &it->block_reader);
	return 0;
}

static int indexed_table_ref_iter_seek(void *p UNUSED,
				       struct reftable_record *want UNUSED)
{
	BUG("seeking indexed table is not supported");
	return -1;
}

static int indexed_table_ref_iter_next(void *p, struct reftable_record *rec)
{
	struct indexed_table_ref_iter *it = p;
	struct reftable_ref_record *ref = &rec->u.ref;

	while (1) {
		int err = block_iter_next(&it->cur, rec);
		if (err < 0) {
			return err;
		}

		if (err > 0) {
			err = indexed_table_ref_iter_next_block(it);
			if (err < 0) {
				return err;
			}

			if (it->is_finished) {
				return 1;
			}
			continue;
		}
		/* BUG */
		if (!memcmp(it->oid.buf, ref->value.val2.target_value,
			    it->oid.len) ||
		    !memcmp(it->oid.buf, ref->value.val2.value, it->oid.len)) {
			return 0;
		}
	}
}

int indexed_table_ref_iter_new(struct indexed_table_ref_iter **dest,
			       struct reftable_reader *r, uint8_t *oid,
			       int oid_len, uint64_t *offsets, int offset_len)
{
	struct indexed_table_ref_iter empty = INDEXED_TABLE_REF_ITER_INIT;
	struct indexed_table_ref_iter *itr;
	int err = 0;

	itr = reftable_calloc(1, sizeof(*itr));
	if (!itr) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto out;
	}

	*itr = empty;
	itr->r = r;

	err = reftable_buf_add(&itr->oid, oid, oid_len);
	if (err < 0)
		goto out;

	itr->offsets = offsets;
	itr->offset_len = offset_len;

	err = indexed_table_ref_iter_next_block(itr);
	if (err < 0)
		goto out;

	*dest = itr;
	err = 0;

out:
	if (err < 0) {
		*dest = NULL;
		reftable_free(itr);
	}
	return err;
}

static struct reftable_iterator_vtable indexed_table_ref_iter_vtable = {
	.seek = &indexed_table_ref_iter_seek,
	.next = &indexed_table_ref_iter_next,
	.close = &indexed_table_ref_iter_close,
};

void iterator_from_indexed_table_ref_iter(struct reftable_iterator *it,
					  struct indexed_table_ref_iter *itr)
{
	assert(!it->ops);
	it->iter_arg = itr;
	it->ops = &indexed_table_ref_iter_vtable;
}

void reftable_iterator_destroy(struct reftable_iterator *it)
{
	if (!it->ops)
		return;
	it->ops->close(it->iter_arg);
	it->ops = NULL;
	REFTABLE_FREE_AND_NULL(it->iter_arg);
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
