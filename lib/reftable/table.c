/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#include "table.h"

#include "system.h"
#include "block.h"
#include "blocksource.h"
#include "constants.h"
#include "iter.h"
#include "record.h"
#include "reftable-error.h"

static struct reftable_table_offsets *
table_offsets_for(struct reftable_table *t, uint8_t typ)
{
	switch (typ) {
	case REFTABLE_BLOCK_TYPE_REF:
		return &t->ref_offsets;
	case REFTABLE_BLOCK_TYPE_LOG:
		return &t->log_offsets;
	case REFTABLE_BLOCK_TYPE_OBJ:
		return &t->obj_offsets;
	}
	abort();
}

enum reftable_hash reftable_table_hash_id(struct reftable_table *t)
{
	return t->hash_id;
}

const char *reftable_table_name(struct reftable_table *t)
{
	return t->name;
}

static int parse_footer(struct reftable_table *t, uint8_t *footer,
			uint8_t *header)
{
	uint8_t *f = footer;
	uint8_t first_block_typ;
	int err = 0;
	uint32_t computed_crc;
	uint32_t file_crc;

	if (memcmp(f, "REFT", 4)) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}
	f += 4;

	if (memcmp(footer, header, header_size(t->version))) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	f++;
	t->block_size = reftable_get_be24(f);

	f += 3;
	t->min_update_index = reftable_get_be64(f);
	f += 8;
	t->max_update_index = reftable_get_be64(f);
	f += 8;

	if (t->version == 1) {
		t->hash_id = REFTABLE_HASH_SHA1;
	} else {
		switch (reftable_get_be32(f)) {
		case REFTABLE_FORMAT_ID_SHA1:
			t->hash_id = REFTABLE_HASH_SHA1;
			break;
		case REFTABLE_FORMAT_ID_SHA256:
			t->hash_id = REFTABLE_HASH_SHA256;
			break;
		default:
			err = REFTABLE_FORMAT_ERROR;
			goto done;
		}

		f += 4;
	}

	t->ref_offsets.index_offset = reftable_get_be64(f);
	f += 8;

	t->obj_offsets.offset = reftable_get_be64(f);
	f += 8;

	t->object_id_len = t->obj_offsets.offset & ((1 << 5) - 1);
	t->obj_offsets.offset >>= 5;

	t->obj_offsets.index_offset = reftable_get_be64(f);
	f += 8;
	t->log_offsets.offset = reftable_get_be64(f);
	f += 8;
	t->log_offsets.index_offset = reftable_get_be64(f);
	f += 8;

	computed_crc = crc32(0, footer, f - footer);
	file_crc = reftable_get_be32(f);
	f += 4;
	if (computed_crc != file_crc) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	first_block_typ = header[header_size(t->version)];
	t->ref_offsets.is_present = (first_block_typ == REFTABLE_BLOCK_TYPE_REF);
	t->ref_offsets.offset = 0;
	t->log_offsets.is_present = (first_block_typ == REFTABLE_BLOCK_TYPE_LOG ||
				     t->log_offsets.offset > 0);
	t->obj_offsets.is_present = t->obj_offsets.offset > 0;
	if (t->obj_offsets.is_present && !t->object_id_len) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	err = 0;
done:
	return err;
}

struct table_iter {
	struct reftable_table *table;
	uint8_t typ;
	uint64_t block_off;
	struct reftable_block block;
	struct block_iter bi;
	int is_finished;
};

static int table_iter_init(struct table_iter *ti, struct reftable_table *t)
{
	struct block_iter bi = BLOCK_ITER_INIT;
	memset(ti, 0, sizeof(*ti));
	reftable_table_incref(t);
	ti->table = t;
	ti->bi = bi;
	return 0;
}

static int table_iter_next_in_block(struct table_iter *ti,
				    struct reftable_record *rec)
{
	int res = block_iter_next(&ti->bi, rec);
	if (res == 0 && reftable_record_type(rec) == REFTABLE_BLOCK_TYPE_REF) {
		rec->u.ref.update_index += ti->table->min_update_index;
	}

	return res;
}

static void table_iter_block_done(struct table_iter *ti)
{
	reftable_block_release(&ti->block);
	block_iter_reset(&ti->bi);
}

int table_init_block(struct reftable_table *t, struct reftable_block *block,
		     uint64_t next_off, uint8_t want_typ)
{
	uint32_t header_off = next_off ? 0 : header_size(t->version);
	int err;

	if (next_off >= t->size)
		return 1;

	err = reftable_block_init(block, &t->source, next_off, header_off,
				  t->block_size, hash_size(t->hash_id), want_typ);
	if (err)
		reftable_block_release(block);
	return err;
}

static void table_iter_close(struct table_iter *ti)
{
	table_iter_block_done(ti);
	block_iter_close(&ti->bi);
	reftable_table_decref(ti->table);
}

static int table_iter_next_block(struct table_iter *ti)
{
	uint64_t next_block_off = ti->block_off + ti->block.full_block_size;
	int err;

	err = table_init_block(ti->table, &ti->block, next_block_off, ti->typ);
	if (err > 0)
		ti->is_finished = 1;
	if (err)
		return err;

	ti->block_off = next_block_off;
	ti->is_finished = 0;
	block_iter_init(&ti->bi, &ti->block);

	return 0;
}

static int table_iter_next(struct table_iter *ti, struct reftable_record *rec)
{
	if (reftable_record_type(rec) != ti->typ)
		return REFTABLE_API_ERROR;

	while (1) {
		int err;

		if (ti->is_finished)
			return 1;

		/*
		 * Check whether the current block still has more records. If
		 * so, return it. If the iterator returns positive then the
		 * current block has been exhausted.
		 */
		err = table_iter_next_in_block(ti, rec);
		if (err <= 0)
			return err;

		/*
		 * Otherwise, we need to continue to the next block in the
		 * table and retry. If there are no more blocks then the
		 * iterator is drained.
		 */
		err = table_iter_next_block(ti);
		if (err) {
			ti->is_finished = 1;
			return err;
		}
	}
}

static int table_iter_seek_to(struct table_iter *ti, uint64_t off, uint8_t typ)
{
	int err;

	err = table_init_block(ti->table, &ti->block, off, typ);
	if (err != 0)
		return err;

	ti->typ = reftable_block_type(&ti->block);
	ti->block_off = off;
	block_iter_init(&ti->bi, &ti->block);
	ti->is_finished = 0;
	return 0;
}

static int table_iter_seek_start(struct table_iter *ti, uint8_t typ, int index)
{
	struct reftable_table_offsets *offs = table_offsets_for(ti->table, typ);
	uint64_t off = offs->offset;
	if (index) {
		off = offs->index_offset;
		if (off == 0) {
			return 1;
		}
		typ = REFTABLE_BLOCK_TYPE_INDEX;
	}

	return table_iter_seek_to(ti, off, typ);
}

static int table_iter_seek_linear(struct table_iter *ti,
				  struct reftable_record *want)
{
	struct reftable_buf want_key = REFTABLE_BUF_INIT;
	struct reftable_buf got_key = REFTABLE_BUF_INIT;
	struct reftable_record rec;
	int err;

	err = reftable_record_init(&rec, reftable_record_type(want));
	if (err < 0)
		goto done;

	err = reftable_record_key(want, &want_key);
	if (err < 0)
		goto done;

	/*
	 * First we need to locate the block that must contain our record. To
	 * do so we scan through blocks linearly until we find the first block
	 * whose first key is bigger than our wanted key. Once we have found
	 * that block we know that the key must be contained in the preceding
	 * block.
	 *
	 * This algorithm is somewhat unfortunate because it means that we
	 * always have to seek one block too far and then back up. But as we
	 * can only decode the _first_ key of a block but not its _last_ key we
	 * have no other way to do this.
	 */
	while (1) {
		struct table_iter next = *ti;

		/*
		 * We must be careful to not modify underlying data of `ti`
		 * because we may find that `next` does not contain our desired
		 * block, but that `ti` does. In that case, we would discard
		 * `next` and continue with `ti`.
		 *
		 * This also means that we cannot reuse allocated memory for
		 * `next` here. While it would be great if we could, it should
		 * in practice not be too bad given that we should only ever
		 * end up doing linear seeks with at most three blocks. As soon
		 * as we have more than three blocks we would have an index, so
		 * we would not do a linear search there anymore.
		 */
		memset(&next.block.block_data, 0, sizeof(next.block.block_data));
		next.block.zstream = NULL;
		next.block.uncompressed_data = NULL;
		next.block.uncompressed_cap = 0;

		err = table_iter_next_block(&next);
		if (err < 0)
			goto done;
		if (err > 0)
			break;

		err = reftable_block_first_key(&next.block, &got_key);
		if (err < 0)
			goto done;

		if (reftable_buf_cmp(&got_key, &want_key) > 0) {
			table_iter_block_done(&next);
			break;
		}

		table_iter_block_done(ti);
		*ti = next;
	}

	/*
	 * We have located the block that must contain our record, so we seek
	 * the wanted key inside of it. If the block does not contain our key
	 * we know that the corresponding record does not exist.
	 */
	block_iter_init(&ti->bi, &ti->block);
	err = block_iter_seek_key(&ti->bi, &want_key);
	if (err < 0)
		goto done;
	err = 0;

done:
	reftable_record_release(&rec);
	reftable_buf_release(&want_key);
	reftable_buf_release(&got_key);
	return err;
}

static int table_iter_seek_indexed(struct table_iter *ti,
				   struct reftable_record *rec)
{
	struct reftable_record want_index = {
		.type = REFTABLE_BLOCK_TYPE_INDEX, .u.idx = { .last_key = REFTABLE_BUF_INIT }
	};
	struct reftable_record index_result = {
		.type = REFTABLE_BLOCK_TYPE_INDEX,
		.u.idx = { .last_key = REFTABLE_BUF_INIT },
	};
	int err;

	err = reftable_record_key(rec, &want_index.u.idx.last_key);
	if (err < 0)
		goto done;

	/*
	 * The index may consist of multiple levels, where each level may have
	 * multiple index blocks. We start by doing a linear search in the
	 * highest layer that identifies the relevant index block as well as
	 * the record inside that block that corresponds to our wanted key.
	 */
	err = table_iter_seek_linear(ti, &want_index);
	if (err < 0)
		goto done;

	/*
	 * Traverse down the levels until we find a non-index entry.
	 */
	while (1) {
		/*
		 * In case we seek a record that does not exist the index iter
		 * will tell us that the iterator is over. This works because
		 * the last index entry of the current level will contain the
		 * last key it knows about. So in case our seeked key is larger
		 * than the last indexed key we know that it won't exist.
		 *
		 * There is one subtlety in the layout of the index section
		 * that makes this work as expected: the highest-level index is
		 * at end of the section and will point backwards and thus we
		 * start reading from the end of the index section, not the
		 * beginning.
		 *
		 * If that wasn't the case and the order was reversed then the
		 * linear seek would seek into the lower levels and traverse
		 * all levels of the index only to find out that the key does
		 * not exist.
		 */
		err = table_iter_next(ti, &index_result);
		if (err != 0)
			goto done;

		err = table_iter_seek_to(ti, index_result.u.idx.offset, 0);
		if (err != 0)
			goto done;

		block_iter_init(&ti->bi, &ti->block);

		err = block_iter_seek_key(&ti->bi, &want_index.u.idx.last_key);
		if (err < 0)
			goto done;

		if (ti->typ == reftable_record_type(rec)) {
			err = 0;
			break;
		}

		if (ti->typ != REFTABLE_BLOCK_TYPE_INDEX) {
			err = REFTABLE_FORMAT_ERROR;
			goto done;
		}
	}

done:
	reftable_record_release(&want_index);
	reftable_record_release(&index_result);
	return err;
}

static int table_iter_seek(struct table_iter *ti,
			   struct reftable_record *want)
{
	uint8_t typ = reftable_record_type(want);
	struct reftable_table_offsets *offs = table_offsets_for(ti->table, typ);
	int err;

	err = table_iter_seek_start(ti, reftable_record_type(want),
				    !!offs->index_offset);
	if (err < 0)
		goto out;

	if (offs->index_offset)
		err = table_iter_seek_indexed(ti, want);
	else
		err = table_iter_seek_linear(ti, want);
	if (err)
		goto out;

out:
	return err;
}

static int table_iter_seek_void(void *ti, struct reftable_record *want)
{
	return table_iter_seek(ti, want);
}

static int table_iter_next_void(void *ti, struct reftable_record *rec)
{
	return table_iter_next(ti, rec);
}

static void table_iter_close_void(void *ti)
{
	table_iter_close(ti);
}

static struct reftable_iterator_vtable table_iter_vtable = {
	.seek = &table_iter_seek_void,
	.next = &table_iter_next_void,
	.close = &table_iter_close_void,
};

static void iterator_from_table_iter(struct reftable_iterator *it,
				     struct table_iter *ti)
{
	assert(!it->ops);
	it->iter_arg = ti;
	it->ops = &table_iter_vtable;
}

int table_init_iter(struct reftable_table *t,
		    struct reftable_iterator *it,
		    uint8_t typ)
{
	struct reftable_table_offsets *offs = table_offsets_for(t, typ);

	if (offs->is_present) {
		struct table_iter *ti;
		REFTABLE_ALLOC_ARRAY(ti, 1);
		if (!ti)
			return REFTABLE_OUT_OF_MEMORY_ERROR;

		table_iter_init(ti, t);
		iterator_from_table_iter(it, ti);
	} else {
		iterator_set_empty(it);
	}

	return 0;
}

int reftable_table_init_ref_iterator(struct reftable_table *t,
				     struct reftable_iterator *it)
{
	return table_init_iter(t, it, REFTABLE_BLOCK_TYPE_REF);
}

int reftable_table_init_log_iterator(struct reftable_table *t,
				     struct reftable_iterator *it)
{
	return table_init_iter(t, it, REFTABLE_BLOCK_TYPE_LOG);
}

int reftable_table_new(struct reftable_table **out,
		       struct reftable_block_source *source, char const *name)
{
	struct reftable_block_data footer = { 0 };
	struct reftable_block_data header = { 0 };
	struct reftable_table *t;
	uint64_t file_size = block_source_size(source);
	uint32_t read_size;
	ssize_t bytes_read;
	int err;

	REFTABLE_CALLOC_ARRAY(t, 1);
	if (!t) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto done;
	}

	/*
	 * We need one extra byte to read the type of first block. We also
	 * pretend to always be reading v2 of the format because it is larger.
	 */
	read_size = header_size(2) + 1;
	if (read_size > file_size) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	bytes_read = block_source_read_data(source, &header, 0, read_size);
	if (bytes_read < 0 || (size_t)bytes_read != read_size) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	if (memcmp(header.data, "REFT", 4)) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}
	t->version = header.data[4];
	if (t->version != 1 && t->version != 2) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	t->size = file_size - footer_size(t->version);
	t->source = *source;
	t->name = reftable_strdup(name);
	if (!t->name) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto done;
	}
	t->hash_id = 0;
	t->refcount = 1;

	bytes_read = block_source_read_data(source, &footer, t->size,
					    footer_size(t->version));
	if (bytes_read < 0 || (size_t)bytes_read != footer_size(t->version)) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	err = parse_footer(t, footer.data, header.data);
	if (err)
		goto done;

	*out = t;

done:
	block_source_release_data(&footer);
	block_source_release_data(&header);
	if (err) {
		if (t)
			reftable_free(t->name);
		reftable_free(t);
		block_source_close(source);
	}
	return err;
}

void reftable_table_incref(struct reftable_table *t)
{
	t->refcount++;
}

void reftable_table_decref(struct reftable_table *t)
{
	if (!t)
		return;
	if (--t->refcount)
		return;
	block_source_close(&t->source);
	REFTABLE_FREE_AND_NULL(t->name);
	reftable_free(t);
}

static int reftable_table_refs_for_indexed(struct reftable_table *t,
					   struct reftable_iterator *it,
					   uint8_t *oid)
{
	struct reftable_record want = {
		.type = REFTABLE_BLOCK_TYPE_OBJ,
		.u.obj = {
			.hash_prefix = oid,
			.hash_prefix_len = t->object_id_len,
		},
	};
	struct reftable_iterator oit = { NULL };
	struct reftable_record got = {
		.type = REFTABLE_BLOCK_TYPE_OBJ,
		.u.obj = { 0 },
	};
	int err = 0;
	struct indexed_table_ref_iter *itr = NULL;

	/* Look through the reverse index. */
	err = table_init_iter(t, &oit, REFTABLE_BLOCK_TYPE_OBJ);
	if (err < 0)
		goto done;

	err = iterator_seek(&oit, &want);
	if (err != 0)
		goto done;

	/* read out the reftable_obj_record */
	err = iterator_next(&oit, &got);
	if (err < 0)
		goto done;

	if (err > 0 || memcmp(want.u.obj.hash_prefix, got.u.obj.hash_prefix,
			      t->object_id_len)) {
		/* didn't find it; return empty iterator */
		iterator_set_empty(it);
		err = 0;
		goto done;
	}

	err = indexed_table_ref_iter_new(&itr, t, oid, hash_size(t->hash_id),
					 got.u.obj.offsets,
					 got.u.obj.offset_len);
	if (err < 0)
		goto done;
	got.u.obj.offsets = NULL;
	iterator_from_indexed_table_ref_iter(it, itr);

done:
	reftable_iterator_destroy(&oit);
	reftable_record_release(&got);
	return err;
}

static int reftable_table_refs_for_unindexed(struct reftable_table *t,
					     struct reftable_iterator *it,
					     uint8_t *oid)
{
	struct table_iter *ti;
	struct filtering_ref_iterator *filter = NULL;
	struct filtering_ref_iterator empty = FILTERING_REF_ITERATOR_INIT;
	uint32_t oid_len = hash_size(t->hash_id);
	int err;

	REFTABLE_ALLOC_ARRAY(ti, 1);
	if (!ti) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto out;
	}

	table_iter_init(ti, t);
	err = table_iter_seek_start(ti, REFTABLE_BLOCK_TYPE_REF, 0);
	if (err < 0)
		goto out;

	filter = reftable_malloc(sizeof(*filter));
	if (!filter) {
		err = REFTABLE_OUT_OF_MEMORY_ERROR;
		goto out;
	}
	*filter = empty;

	err = reftable_buf_add(&filter->oid, oid, oid_len);
	if (err < 0)
		goto out;

	iterator_from_table_iter(&filter->it, ti);

	iterator_from_filtering_ref_iterator(it, filter);

	err = 0;

out:
	if (err < 0) {
		if (ti)
			table_iter_close(ti);
		reftable_free(ti);
	}
	return err;
}

int reftable_table_refs_for(struct reftable_table *t,
			    struct reftable_iterator *it, uint8_t *oid)
{
	if (t->obj_offsets.is_present)
		return reftable_table_refs_for_indexed(t, it, oid);
	return reftable_table_refs_for_unindexed(t, it, oid);
}

uint64_t reftable_table_max_update_index(struct reftable_table *t)
{
	return t->max_update_index;
}

uint64_t reftable_table_min_update_index(struct reftable_table *t)
{
	return t->min_update_index;
}

int reftable_table_iterator_init(struct reftable_table_iterator *it,
				 struct reftable_table *t)
{
	struct table_iter *ti;
	int err;

	REFTABLE_ALLOC_ARRAY(ti, 1);
	if (!ti)
		return REFTABLE_OUT_OF_MEMORY_ERROR;

	err = table_iter_init(ti, t);
	if (err < 0)
		goto out;

	it->iter_arg = ti;
	err = 0;

out:
	if (err < 0)
		reftable_free(ti);
	return err;
}

void reftable_table_iterator_release(struct reftable_table_iterator *it)
{
	if (!it->iter_arg)
		return;
	table_iter_close(it->iter_arg);
	reftable_free(it->iter_arg);
	it->iter_arg = NULL;
}

int reftable_table_iterator_next(struct reftable_table_iterator *it,
				 const struct reftable_block **out)
{
	struct table_iter *ti = it->iter_arg;
	int err;

	err = table_iter_next_block(ti);
	if (err)
		return err;

	*out = &ti->block;

	return 0;
}
