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
#include "reftable.h"

bool iterator_is_null(struct iterator it)
{
	return it.ops == NULL;
}

static int empty_iterator_next(void *arg, struct record rec)
{
	return 1;
}

static void empty_iterator_close(void *arg)
{
}

struct iterator_vtable empty_vtable = {
	.next = &empty_iterator_next,
	.close = &empty_iterator_close,
};

void iterator_set_empty(struct iterator *it)
{
	it->iter_arg = NULL;
	it->ops = &empty_vtable;
}

int iterator_next(struct iterator it, struct record rec)
{
	return it.ops->next(it.iter_arg, rec);
}

void iterator_destroy(struct iterator *it)
{
	if (it->ops == NULL) {
		return;
	}
	it->ops->close(it->iter_arg);
	it->ops = NULL;
	FREE_AND_NULL(it->iter_arg);
}

int iterator_next_ref(struct iterator it, struct ref_record *ref)
{
	struct record rec = {};
	record_from_ref(&rec, ref);
	return iterator_next(it, rec);
}

int iterator_next_log(struct iterator it, struct log_record *log)
{
	struct record rec = {};
	record_from_log(&rec, log);
	return iterator_next(it, rec);
}

static void filtering_ref_iterator_close(void *iter_arg)
{
	struct filtering_ref_iterator *fri =
		(struct filtering_ref_iterator *)iter_arg;
	free(slice_yield(&fri->oid));
	iterator_destroy(&fri->it);
}

static int filtering_ref_iterator_next(void *iter_arg, struct record rec)
{
	struct filtering_ref_iterator *fri =
		(struct filtering_ref_iterator *)iter_arg;
	struct ref_record *ref = (struct ref_record *)rec.data;

	while (true) {
		int err = iterator_next_ref(fri->it, ref);
		if (err != 0) {
			return err;
		}

		if (fri->double_check) {
			struct iterator it = {};

			int err = reader_seek_ref(fri->r, &it, ref->ref_name);
			if (err == 0) {
				err = iterator_next_ref(it, ref);
			}

			iterator_destroy(&it);

			if (err < 0) {
				return err;
			}

			if (err > 0) {
				continue;
			}
		}

		if ((ref->target_value != NULL &&
		     !memcmp(fri->oid.buf, ref->target_value, fri->oid.len)) ||
		    (ref->value != NULL &&
		     !memcmp(fri->oid.buf, ref->value, fri->oid.len))) {
			return 0;
		}
	}
}

struct iterator_vtable filtering_ref_iterator_vtable = {
	.next = &filtering_ref_iterator_next,
	.close = &filtering_ref_iterator_close,
};

void iterator_from_filtering_ref_iterator(struct iterator *it,
					  struct filtering_ref_iterator *fri)
{
	it->iter_arg = fri;
	it->ops = &filtering_ref_iterator_vtable;
}

static void indexed_table_ref_iter_close(void *p)
{
	struct indexed_table_ref_iter *it = (struct indexed_table_ref_iter *)p;
	block_iter_close(&it->cur);
	reader_return_block(it->r, &it->block_reader.block);
	free(slice_yield(&it->oid));
}

static int indexed_table_ref_iter_next_block(struct indexed_table_ref_iter *it)
{
	if (it->offset_idx == it->offset_len) {
		it->finished = true;
		return 1;
	}

	reader_return_block(it->r, &it->block_reader.block);

	{
		uint64_t off = it->offsets[it->offset_idx++];
		int err = reader_init_block_reader(it->r, &it->block_reader,
						   off, BLOCK_TYPE_REF);
		if (err < 0) {
			return err;
		}
		if (err > 0) {
			/* indexed block does not exist. */
			return FORMAT_ERROR;
		}
	}
	block_reader_start(&it->block_reader, &it->cur);
	return 0;
}

static int indexed_table_ref_iter_next(void *p, struct record rec)
{
	struct indexed_table_ref_iter *it = (struct indexed_table_ref_iter *)p;
	struct ref_record *ref = (struct ref_record *)rec.data;

	while (true) {
		int err = block_iter_next(&it->cur, rec);
		if (err < 0) {
			return err;
		}

		if (err > 0) {
			err = indexed_table_ref_iter_next_block(it);
			if (err < 0) {
				return err;
			}

			if (it->finished) {
				return 1;
			}
			continue;
		}

		if (!memcmp(it->oid.buf, ref->target_value, it->oid.len) ||
		    !memcmp(it->oid.buf, ref->value, it->oid.len)) {
			return 0;
		}
	}
}

int new_indexed_table_ref_iter(struct indexed_table_ref_iter **dest,
			       struct reader *r, byte *oid, int oid_len,
			       uint64_t *offsets, int offset_len)
{
	struct indexed_table_ref_iter *itr =
		calloc(sizeof(struct indexed_table_ref_iter), 1);
	int err = 0;

	itr->r = r;
	slice_resize(&itr->oid, oid_len);
	memcpy(itr->oid.buf, oid, oid_len);

	itr->offsets = offsets;
	itr->offset_len = offset_len;

	err = indexed_table_ref_iter_next_block(itr);
	if (err < 0) {
		free(itr);
	} else {
		*dest = itr;
	}
	return err;
}

struct iterator_vtable indexed_table_ref_iter_vtable = {
	.next = &indexed_table_ref_iter_next,
	.close = &indexed_table_ref_iter_close,
};

void iterator_from_indexed_table_ref_iter(struct iterator *it,
					  struct indexed_table_ref_iter *itr)
{
	it->iter_arg = itr;
	it->ops = &indexed_table_ref_iter_vtable;
}
