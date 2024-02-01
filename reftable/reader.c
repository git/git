/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "reader.h"

#include "system.h"
#include "block.h"
#include "constants.h"
#include "generic.h"
#include "iter.h"
#include "record.h"
#include "reftable-error.h"
#include "reftable-generic.h"

uint64_t block_source_size(struct reftable_block_source *source)
{
	return source->ops->size(source->arg);
}

int block_source_read_block(struct reftable_block_source *source,
			    struct reftable_block *dest, uint64_t off,
			    uint32_t size)
{
	int result = source->ops->read_block(source->arg, dest, off, size);
	dest->source = *source;
	return result;
}

void block_source_close(struct reftable_block_source *source)
{
	if (!source->ops) {
		return;
	}

	source->ops->close(source->arg);
	source->ops = NULL;
}

static struct reftable_reader_offsets *
reader_offsets_for(struct reftable_reader *r, uint8_t typ)
{
	switch (typ) {
	case BLOCK_TYPE_REF:
		return &r->ref_offsets;
	case BLOCK_TYPE_LOG:
		return &r->log_offsets;
	case BLOCK_TYPE_OBJ:
		return &r->obj_offsets;
	}
	abort();
}

static int reader_get_block(struct reftable_reader *r,
			    struct reftable_block *dest, uint64_t off,
			    uint32_t sz)
{
	if (off >= r->size)
		return 0;

	if (off + sz > r->size) {
		sz = r->size - off;
	}

	return block_source_read_block(&r->source, dest, off, sz);
}

uint32_t reftable_reader_hash_id(struct reftable_reader *r)
{
	return r->hash_id;
}

const char *reader_name(struct reftable_reader *r)
{
	return r->name;
}

static int parse_footer(struct reftable_reader *r, uint8_t *footer,
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

	if (memcmp(footer, header, header_size(r->version))) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	f++;
	r->block_size = get_be24(f);

	f += 3;
	r->min_update_index = get_be64(f);
	f += 8;
	r->max_update_index = get_be64(f);
	f += 8;

	if (r->version == 1) {
		r->hash_id = GIT_SHA1_FORMAT_ID;
	} else {
		r->hash_id = get_be32(f);
		switch (r->hash_id) {
		case GIT_SHA1_FORMAT_ID:
			break;
		case GIT_SHA256_FORMAT_ID:
			break;
		default:
			err = REFTABLE_FORMAT_ERROR;
			goto done;
		}
		f += 4;
	}

	r->ref_offsets.index_offset = get_be64(f);
	f += 8;

	r->obj_offsets.offset = get_be64(f);
	f += 8;

	r->object_id_len = r->obj_offsets.offset & ((1 << 5) - 1);
	r->obj_offsets.offset >>= 5;

	r->obj_offsets.index_offset = get_be64(f);
	f += 8;
	r->log_offsets.offset = get_be64(f);
	f += 8;
	r->log_offsets.index_offset = get_be64(f);
	f += 8;

	computed_crc = crc32(0, footer, f - footer);
	file_crc = get_be32(f);
	f += 4;
	if (computed_crc != file_crc) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	first_block_typ = header[header_size(r->version)];
	r->ref_offsets.is_present = (first_block_typ == BLOCK_TYPE_REF);
	r->ref_offsets.offset = 0;
	r->log_offsets.is_present = (first_block_typ == BLOCK_TYPE_LOG ||
				     r->log_offsets.offset > 0);
	r->obj_offsets.is_present = r->obj_offsets.offset > 0;
	if (r->obj_offsets.is_present && !r->object_id_len) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	err = 0;
done:
	return err;
}

int init_reader(struct reftable_reader *r, struct reftable_block_source *source,
		const char *name)
{
	struct reftable_block footer = { NULL };
	struct reftable_block header = { NULL };
	int err = 0;
	uint64_t file_size = block_source_size(source);

	/* Need +1 to read type of first block. */
	uint32_t read_size = header_size(2) + 1; /* read v2 because it's larger.  */
	memset(r, 0, sizeof(struct reftable_reader));

	if (read_size > file_size) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	err = block_source_read_block(source, &header, 0, read_size);
	if (err != read_size) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	if (memcmp(header.data, "REFT", 4)) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}
	r->version = header.data[4];
	if (r->version != 1 && r->version != 2) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	r->size = file_size - footer_size(r->version);
	r->source = *source;
	r->name = xstrdup(name);
	r->hash_id = 0;

	err = block_source_read_block(source, &footer, r->size,
				      footer_size(r->version));
	if (err != footer_size(r->version)) {
		err = REFTABLE_IO_ERROR;
		goto done;
	}

	err = parse_footer(r, footer.data, header.data);
done:
	reftable_block_done(&footer);
	reftable_block_done(&header);
	return err;
}

struct table_iter {
	struct reftable_reader *r;
	uint8_t typ;
	uint64_t block_off;
	struct block_iter bi;
	int is_finished;
};
#define TABLE_ITER_INIT { \
	.bi = BLOCK_ITER_INIT \
}

static void table_iter_copy_from(struct table_iter *dest,
				 struct table_iter *src)
{
	dest->r = src->r;
	dest->typ = src->typ;
	dest->block_off = src->block_off;
	dest->is_finished = src->is_finished;
	block_iter_copy_from(&dest->bi, &src->bi);
}

static int table_iter_next_in_block(struct table_iter *ti,
				    struct reftable_record *rec)
{
	int res = block_iter_next(&ti->bi, rec);
	if (res == 0 && reftable_record_type(rec) == BLOCK_TYPE_REF) {
		rec->u.ref.update_index += ti->r->min_update_index;
	}

	return res;
}

static void table_iter_block_done(struct table_iter *ti)
{
	if (!ti->bi.br) {
		return;
	}
	reftable_block_done(&ti->bi.br->block);
	FREE_AND_NULL(ti->bi.br);

	ti->bi.last_key.len = 0;
	ti->bi.next_off = 0;
}

static int32_t extract_block_size(uint8_t *data, uint8_t *typ, uint64_t off,
				  int version)
{
	int32_t result = 0;

	if (off == 0) {
		data += header_size(version);
	}

	*typ = data[0];
	if (reftable_is_block_type(*typ)) {
		result = get_be24(data + 1);
	}
	return result;
}

int reader_init_block_reader(struct reftable_reader *r, struct block_reader *br,
			     uint64_t next_off, uint8_t want_typ)
{
	int32_t guess_block_size = r->block_size ? r->block_size :
							 DEFAULT_BLOCK_SIZE;
	struct reftable_block block = { NULL };
	uint8_t block_typ = 0;
	int err = 0;
	uint32_t header_off = next_off ? 0 : header_size(r->version);
	int32_t block_size = 0;

	if (next_off >= r->size)
		return 1;

	err = reader_get_block(r, &block, next_off, guess_block_size);
	if (err < 0)
		goto done;

	block_size = extract_block_size(block.data, &block_typ, next_off,
					r->version);
	if (block_size < 0) {
		err = block_size;
		goto done;
	}
	if (want_typ != BLOCK_TYPE_ANY && block_typ != want_typ) {
		err = 1;
		goto done;
	}

	if (block_size > guess_block_size) {
		reftable_block_done(&block);
		err = reader_get_block(r, &block, next_off, block_size);
		if (err < 0) {
			goto done;
		}
	}

	err = block_reader_init(br, &block, header_off, r->block_size,
				hash_size(r->hash_id));
done:
	reftable_block_done(&block);

	return err;
}

static int table_iter_next_block(struct table_iter *dest,
				 struct table_iter *src)
{
	uint64_t next_block_off = src->block_off + src->bi.br->full_block_size;
	struct block_reader br = { 0 };
	int err = 0;

	dest->r = src->r;
	dest->typ = src->typ;
	dest->block_off = next_block_off;

	err = reader_init_block_reader(src->r, &br, next_block_off, src->typ);
	if (err > 0) {
		dest->is_finished = 1;
		return 1;
	}
	if (err != 0)
		return err;
	else {
		struct block_reader *brp =
			reftable_malloc(sizeof(struct block_reader));
		*brp = br;

		dest->is_finished = 0;
		block_reader_start(brp, &dest->bi);
	}
	return 0;
}

static int table_iter_next(struct table_iter *ti, struct reftable_record *rec)
{
	if (reftable_record_type(rec) != ti->typ)
		return REFTABLE_API_ERROR;

	while (1) {
		struct table_iter next = TABLE_ITER_INIT;
		int err = 0;
		if (ti->is_finished) {
			return 1;
		}

		err = table_iter_next_in_block(ti, rec);
		if (err <= 0) {
			return err;
		}

		err = table_iter_next_block(&next, ti);
		if (err != 0) {
			ti->is_finished = 1;
		}
		table_iter_block_done(ti);
		if (err != 0) {
			return err;
		}
		table_iter_copy_from(ti, &next);
		block_iter_close(&next.bi);
	}
}

static int table_iter_next_void(void *ti, struct reftable_record *rec)
{
	return table_iter_next(ti, rec);
}

static void table_iter_close(void *p)
{
	struct table_iter *ti = p;
	table_iter_block_done(ti);
	block_iter_close(&ti->bi);
}

static struct reftable_iterator_vtable table_iter_vtable = {
	.next = &table_iter_next_void,
	.close = &table_iter_close,
};

static void iterator_from_table_iter(struct reftable_iterator *it,
				     struct table_iter *ti)
{
	assert(!it->ops);
	it->iter_arg = ti;
	it->ops = &table_iter_vtable;
}

static int reader_table_iter_at(struct reftable_reader *r,
				struct table_iter *ti, uint64_t off,
				uint8_t typ)
{
	struct block_reader br = { 0 };
	struct block_reader *brp = NULL;

	int err = reader_init_block_reader(r, &br, off, typ);
	if (err != 0)
		return err;

	brp = reftable_malloc(sizeof(struct block_reader));
	*brp = br;
	ti->r = r;
	ti->typ = block_reader_type(brp);
	ti->block_off = off;
	block_reader_start(brp, &ti->bi);
	return 0;
}

static int reader_start(struct reftable_reader *r, struct table_iter *ti,
			uint8_t typ, int index)
{
	struct reftable_reader_offsets *offs = reader_offsets_for(r, typ);
	uint64_t off = offs->offset;
	if (index) {
		off = offs->index_offset;
		if (off == 0) {
			return 1;
		}
		typ = BLOCK_TYPE_INDEX;
	}

	return reader_table_iter_at(r, ti, off, typ);
}

static int reader_seek_linear(struct table_iter *ti,
			      struct reftable_record *want)
{
	struct reftable_record rec =
		reftable_new_record(reftable_record_type(want));
	struct strbuf want_key = STRBUF_INIT;
	struct strbuf got_key = STRBUF_INIT;
	struct table_iter next = TABLE_ITER_INIT;
	int err = -1;

	reftable_record_key(want, &want_key);

	while (1) {
		err = table_iter_next_block(&next, ti);
		if (err < 0)
			goto done;

		if (err > 0) {
			break;
		}

		err = block_reader_first_key(next.bi.br, &got_key);
		if (err < 0)
			goto done;

		if (strbuf_cmp(&got_key, &want_key) > 0) {
			table_iter_block_done(&next);
			break;
		}

		table_iter_block_done(ti);
		table_iter_copy_from(ti, &next);
	}

	err = block_iter_seek(&ti->bi, &want_key);
	if (err < 0)
		goto done;
	err = 0;

done:
	block_iter_close(&next.bi);
	reftable_record_release(&rec);
	strbuf_release(&want_key);
	strbuf_release(&got_key);
	return err;
}

static int reader_seek_indexed(struct reftable_reader *r,
			       struct reftable_iterator *it,
			       struct reftable_record *rec)
{
	struct reftable_record want_index = {
		.type = BLOCK_TYPE_INDEX, .u.idx = { .last_key = STRBUF_INIT }
	};
	struct reftable_record index_result = {
		.type = BLOCK_TYPE_INDEX,
		.u.idx = { .last_key = STRBUF_INIT },
	};
	struct table_iter index_iter = TABLE_ITER_INIT;
	struct table_iter next = TABLE_ITER_INIT;
	int err = 0;

	reftable_record_key(rec, &want_index.u.idx.last_key);
	err = reader_start(r, &index_iter, reftable_record_type(rec), 1);
	if (err < 0)
		goto done;

	/*
	 * The index may consist of multiple levels, where each level may have
	 * multiple index blocks. We start by doing a linear search in the
	 * highest layer that identifies the relevant index block as well as
	 * the record inside that block that corresponds to our wanted key.
	 */
	err = reader_seek_linear(&index_iter, &want_index);
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
		err = table_iter_next(&index_iter, &index_result);
		table_iter_block_done(&index_iter);
		if (err != 0)
			goto done;

		err = reader_table_iter_at(r, &next, index_result.u.idx.offset,
					   0);
		if (err != 0)
			goto done;

		err = block_iter_seek(&next.bi, &want_index.u.idx.last_key);
		if (err < 0)
			goto done;

		if (next.typ == reftable_record_type(rec)) {
			err = 0;
			break;
		}

		if (next.typ != BLOCK_TYPE_INDEX) {
			err = REFTABLE_FORMAT_ERROR;
			break;
		}

		table_iter_copy_from(&index_iter, &next);
	}

	if (err == 0) {
		struct table_iter empty = TABLE_ITER_INIT;
		struct table_iter *malloced =
			reftable_calloc(sizeof(struct table_iter));
		*malloced = empty;
		table_iter_copy_from(malloced, &next);
		iterator_from_table_iter(it, malloced);
	}
done:
	block_iter_close(&next.bi);
	table_iter_close(&index_iter);
	reftable_record_release(&want_index);
	reftable_record_release(&index_result);
	return err;
}

static int reader_seek_internal(struct reftable_reader *r,
				struct reftable_iterator *it,
				struct reftable_record *rec)
{
	struct reftable_reader_offsets *offs =
		reader_offsets_for(r, reftable_record_type(rec));
	uint64_t idx = offs->index_offset;
	struct table_iter ti = TABLE_ITER_INIT;
	int err = 0;
	if (idx > 0)
		return reader_seek_indexed(r, it, rec);

	err = reader_start(r, &ti, reftable_record_type(rec), 0);
	if (err < 0)
		return err;
	err = reader_seek_linear(&ti, rec);
	if (err < 0)
		return err;
	else {
		struct table_iter *p =
			reftable_malloc(sizeof(struct table_iter));
		*p = ti;
		iterator_from_table_iter(it, p);
	}

	return 0;
}

static int reader_seek(struct reftable_reader *r, struct reftable_iterator *it,
		       struct reftable_record *rec)
{
	uint8_t typ = reftable_record_type(rec);

	struct reftable_reader_offsets *offs = reader_offsets_for(r, typ);
	if (!offs->is_present) {
		iterator_set_empty(it);
		return 0;
	}

	return reader_seek_internal(r, it, rec);
}

int reftable_reader_seek_ref(struct reftable_reader *r,
			     struct reftable_iterator *it, const char *name)
{
	struct reftable_record rec = {
		.type = BLOCK_TYPE_REF,
		.u.ref = {
			.refname = (char *)name,
		},
	};
	return reader_seek(r, it, &rec);
}

int reftable_reader_seek_log_at(struct reftable_reader *r,
				struct reftable_iterator *it, const char *name,
				uint64_t update_index)
{
	struct reftable_record rec = { .type = BLOCK_TYPE_LOG,
				       .u.log = {
					       .refname = (char *)name,
					       .update_index = update_index,
				       } };
	return reader_seek(r, it, &rec);
}

int reftable_reader_seek_log(struct reftable_reader *r,
			     struct reftable_iterator *it, const char *name)
{
	uint64_t max = ~((uint64_t)0);
	return reftable_reader_seek_log_at(r, it, name, max);
}

void reader_close(struct reftable_reader *r)
{
	block_source_close(&r->source);
	FREE_AND_NULL(r->name);
}

int reftable_new_reader(struct reftable_reader **p,
			struct reftable_block_source *src, char const *name)
{
	struct reftable_reader *rd =
		reftable_calloc(sizeof(struct reftable_reader));
	int err = init_reader(rd, src, name);
	if (err == 0) {
		*p = rd;
	} else {
		block_source_close(src);
		reftable_free(rd);
	}
	return err;
}

void reftable_reader_free(struct reftable_reader *r)
{
	if (!r)
		return;
	reader_close(r);
	reftable_free(r);
}

static int reftable_reader_refs_for_indexed(struct reftable_reader *r,
					    struct reftable_iterator *it,
					    uint8_t *oid)
{
	struct reftable_record want = {
		.type = BLOCK_TYPE_OBJ,
		.u.obj = {
			.hash_prefix = oid,
			.hash_prefix_len = r->object_id_len,
		},
	};
	struct reftable_iterator oit = { NULL };
	struct reftable_record got = {
		.type = BLOCK_TYPE_OBJ,
		.u.obj = { 0 },
	};
	int err = 0;
	struct indexed_table_ref_iter *itr = NULL;

	/* Look through the reverse index. */
	err = reader_seek(r, &oit, &want);
	if (err != 0)
		goto done;

	/* read out the reftable_obj_record */
	err = iterator_next(&oit, &got);
	if (err < 0)
		goto done;

	if (err > 0 || memcmp(want.u.obj.hash_prefix, got.u.obj.hash_prefix,
			      r->object_id_len)) {
		/* didn't find it; return empty iterator */
		iterator_set_empty(it);
		err = 0;
		goto done;
	}

	err = new_indexed_table_ref_iter(&itr, r, oid, hash_size(r->hash_id),
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

static int reftable_reader_refs_for_unindexed(struct reftable_reader *r,
					      struct reftable_iterator *it,
					      uint8_t *oid)
{
	struct table_iter ti_empty = TABLE_ITER_INIT;
	struct table_iter *ti = reftable_calloc(sizeof(struct table_iter));
	struct filtering_ref_iterator *filter = NULL;
	struct filtering_ref_iterator empty = FILTERING_REF_ITERATOR_INIT;
	int oid_len = hash_size(r->hash_id);
	int err;

	*ti = ti_empty;
	err = reader_start(r, ti, BLOCK_TYPE_REF, 0);
	if (err < 0) {
		reftable_free(ti);
		return err;
	}

	filter = reftable_malloc(sizeof(struct filtering_ref_iterator));
	*filter = empty;

	strbuf_add(&filter->oid, oid, oid_len);
	reftable_table_from_reader(&filter->tab, r);
	filter->double_check = 0;
	iterator_from_table_iter(&filter->it, ti);

	iterator_from_filtering_ref_iterator(it, filter);
	return 0;
}

int reftable_reader_refs_for(struct reftable_reader *r,
			     struct reftable_iterator *it, uint8_t *oid)
{
	if (r->obj_offsets.is_present)
		return reftable_reader_refs_for_indexed(r, it, oid);
	return reftable_reader_refs_for_unindexed(r, it, oid);
}

uint64_t reftable_reader_max_update_index(struct reftable_reader *r)
{
	return r->max_update_index;
}

uint64_t reftable_reader_min_update_index(struct reftable_reader *r)
{
	return r->min_update_index;
}

/* generic table interface. */

static int reftable_reader_seek_void(void *tab, struct reftable_iterator *it,
				     struct reftable_record *rec)
{
	return reader_seek(tab, it, rec);
}

static uint32_t reftable_reader_hash_id_void(void *tab)
{
	return reftable_reader_hash_id(tab);
}

static uint64_t reftable_reader_min_update_index_void(void *tab)
{
	return reftable_reader_min_update_index(tab);
}

static uint64_t reftable_reader_max_update_index_void(void *tab)
{
	return reftable_reader_max_update_index(tab);
}

static struct reftable_table_vtable reader_vtable = {
	.seek_record = reftable_reader_seek_void,
	.hash_id = reftable_reader_hash_id_void,
	.min_update_index = reftable_reader_min_update_index_void,
	.max_update_index = reftable_reader_max_update_index_void,
};

void reftable_table_from_reader(struct reftable_table *tab,
				struct reftable_reader *reader)
{
	assert(!tab->ops);
	tab->ops = &reader_vtable;
	tab->table_arg = reader;
}


int reftable_reader_print_file(const char *tablename)
{
	struct reftable_block_source src = { NULL };
	int err = reftable_block_source_from_file(&src, tablename);
	struct reftable_reader *r = NULL;
	struct reftable_table tab = { NULL };
	if (err < 0)
		goto done;

	err = reftable_new_reader(&r, &src, tablename);
	if (err < 0)
		goto done;

	reftable_table_from_reader(&tab, r);
	err = reftable_table_print(&tab);
done:
	reftable_reader_free(r);
	return err;
}
