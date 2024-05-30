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
	struct block_reader br;
	struct block_iter bi;
	int is_finished;
};

static int table_iter_init(struct table_iter *ti, struct reftable_reader *r)
{
	struct block_iter bi = BLOCK_ITER_INIT;
	memset(ti, 0, sizeof(*ti));
	ti->r = r;
	ti->bi = bi;
	return 0;
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
	block_reader_release(&ti->br);
	block_iter_reset(&ti->bi);
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

static void table_iter_close(struct table_iter *ti)
{
	table_iter_block_done(ti);
	block_iter_close(&ti->bi);
}

static int table_iter_next_block(struct table_iter *ti)
{
	uint64_t next_block_off = ti->block_off + ti->br.full_block_size;
	int err;

	err = reader_init_block_reader(ti->r, &ti->br, next_block_off, ti->typ);
	if (err > 0)
		ti->is_finished = 1;
	if (err)
		return err;

	ti->block_off = next_block_off;
	ti->is_finished = 0;
	block_iter_seek_start(&ti->bi, &ti->br);

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

	err = reader_init_block_reader(ti->r, &ti->br, off, typ);
	if (err != 0)
		return err;

	ti->typ = block_reader_type(&ti->br);
	ti->block_off = off;
	block_iter_seek_start(&ti->bi, &ti->br);
	return 0;
}

static int table_iter_seek_start(struct table_iter *ti, uint8_t typ, int index)
{
	struct reftable_reader_offsets *offs = reader_offsets_for(ti->r, typ);
	uint64_t off = offs->offset;
	if (index) {
		off = offs->index_offset;
		if (off == 0) {
			return 1;
		}
		typ = BLOCK_TYPE_INDEX;
	}

	return table_iter_seek_to(ti, off, typ);
}

static int table_iter_seek_linear(struct table_iter *ti,
				  struct reftable_record *want)
{
	struct strbuf want_key = STRBUF_INIT;
	struct strbuf got_key = STRBUF_INIT;
	struct reftable_record rec;
	int err;

	reftable_record_init(&rec, reftable_record_type(want));
	reftable_record_key(want, &want_key);

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
		memset(&next.br.block, 0, sizeof(next.br.block));
		next.br.zstream = NULL;
		next.br.uncompressed_data = NULL;
		next.br.uncompressed_cap = 0;

		err = table_iter_next_block(&next);
		if (err < 0)
			goto done;
		if (err > 0)
			break;

		err = block_reader_first_key(&next.br, &got_key);
		if (err < 0)
			goto done;

		if (strbuf_cmp(&got_key, &want_key) > 0) {
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
	err = block_iter_seek_key(&ti->bi, &ti->br, &want_key);
	if (err < 0)
		goto done;
	err = 0;

done:
	reftable_record_release(&rec);
	strbuf_release(&want_key);
	strbuf_release(&got_key);
	return err;
}

static int table_iter_seek_indexed(struct table_iter *ti,
				   struct reftable_record *rec)
{
	struct reftable_record want_index = {
		.type = BLOCK_TYPE_INDEX, .u.idx = { .last_key = STRBUF_INIT }
	};
	struct reftable_record index_result = {
		.type = BLOCK_TYPE_INDEX,
		.u.idx = { .last_key = STRBUF_INIT },
	};
	int err;

	reftable_record_key(rec, &want_index.u.idx.last_key);

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

		err = block_iter_seek_key(&ti->bi, &ti->br, &want_index.u.idx.last_key);
		if (err < 0)
			goto done;

		if (ti->typ == reftable_record_type(rec)) {
			err = 0;
			break;
		}

		if (ti->typ != BLOCK_TYPE_INDEX) {
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
	struct reftable_reader_offsets *offs = reader_offsets_for(ti->r, typ);
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

static void reader_init_iter(struct reftable_reader *r,
			     struct reftable_iterator *it,
			     uint8_t typ)
{
	struct reftable_reader_offsets *offs = reader_offsets_for(r, typ);

	if (offs->is_present) {
		struct table_iter *ti;
		REFTABLE_ALLOC_ARRAY(ti, 1);
		table_iter_init(ti, r);
		iterator_from_table_iter(it, ti);
	} else {
		iterator_set_empty(it);
	}
}

void reftable_reader_init_ref_iterator(struct reftable_reader *r,
				       struct reftable_iterator *it)
{
	reader_init_iter(r, it, BLOCK_TYPE_REF);
}

void reftable_reader_init_log_iterator(struct reftable_reader *r,
				       struct reftable_iterator *it)
{
	reader_init_iter(r, it, BLOCK_TYPE_LOG);
}

void reader_close(struct reftable_reader *r)
{
	block_source_close(&r->source);
	FREE_AND_NULL(r->name);
}

int reftable_new_reader(struct reftable_reader **p,
			struct reftable_block_source *src, char const *name)
{
	struct reftable_reader *rd = reftable_calloc(1, sizeof(*rd));
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
	reader_init_iter(r, &oit, BLOCK_TYPE_OBJ);
	err = iterator_seek(&oit, &want);
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
	struct table_iter *ti;
	struct filtering_ref_iterator *filter = NULL;
	struct filtering_ref_iterator empty = FILTERING_REF_ITERATOR_INIT;
	int oid_len = hash_size(r->hash_id);
	int err;

	REFTABLE_ALLOC_ARRAY(ti, 1);
	table_iter_init(ti, r);
	err = table_iter_seek_start(ti, BLOCK_TYPE_REF, 0);
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

static void reftable_reader_init_iter_void(void *tab,
					   struct reftable_iterator *it,
					   uint8_t typ)
{
	reader_init_iter(tab, it, typ);
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
	.init_iter = reftable_reader_init_iter_void,
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

int reftable_reader_print_blocks(const char *tablename)
{
	struct {
		const char *name;
		int type;
	} sections[] = {
		{
			.name = "ref",
			.type = BLOCK_TYPE_REF,
		},
		{
			.name = "obj",
			.type = BLOCK_TYPE_OBJ,
		},
		{
			.name = "log",
			.type = BLOCK_TYPE_LOG,
		},
	};
	struct reftable_block_source src = { 0 };
	struct reftable_reader *r = NULL;
	struct table_iter ti = { 0 };
	size_t i;
	int err;

	err = reftable_block_source_from_file(&src, tablename);
	if (err < 0)
		goto done;

	err = reftable_new_reader(&r, &src, tablename);
	if (err < 0)
		goto done;

	table_iter_init(&ti, r);

	printf("header:\n");
	printf("  block_size: %d\n", r->block_size);

	for (i = 0; i < ARRAY_SIZE(sections); i++) {
		err = table_iter_seek_start(&ti, sections[i].type, 0);
		if (err < 0)
			goto done;
		if (err > 0)
			continue;

		printf("%s:\n", sections[i].name);

		while (1) {
			printf("  - length: %u\n", ti.br.block_len);
			printf("    restarts: %u\n", ti.br.restart_count);

			err = table_iter_next_block(&ti);
			if (err < 0)
				goto done;
			if (err > 0)
				break;
		}
	}

done:
	reftable_reader_free(r);
	table_iter_close(&ti);
	return err;
}
