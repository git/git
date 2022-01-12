/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "block.h"

#include "blocksource.h"
#include "constants.h"
#include "record.h"
#include "reftable-error.h"
#include "system.h"
#include <zlib.h>

int header_size(int version)
{
	switch (version) {
	case 1:
		return 24;
	case 2:
		return 28;
	}
	abort();
}

int footer_size(int version)
{
	switch (version) {
	case 1:
		return 68;
	case 2:
		return 72;
	}
	abort();
}

static int block_writer_register_restart(struct block_writer *w, int n,
					 int is_restart, struct strbuf *key)
{
	int rlen = w->restart_len;
	if (rlen >= MAX_RESTARTS) {
		is_restart = 0;
	}

	if (is_restart) {
		rlen++;
	}
	if (2 + 3 * rlen + n > w->block_size - w->next)
		return -1;
	if (is_restart) {
		if (w->restart_len == w->restart_cap) {
			w->restart_cap = w->restart_cap * 2 + 1;
			w->restarts = reftable_realloc(
				w->restarts, sizeof(uint32_t) * w->restart_cap);
		}

		w->restarts[w->restart_len++] = w->next;
	}

	w->next += n;

	strbuf_reset(&w->last_key);
	strbuf_addbuf(&w->last_key, key);
	w->entries++;
	return 0;
}

void block_writer_init(struct block_writer *bw, uint8_t typ, uint8_t *buf,
		       uint32_t block_size, uint32_t header_off, int hash_size)
{
	bw->buf = buf;
	bw->hash_size = hash_size;
	bw->block_size = block_size;
	bw->header_off = header_off;
	bw->buf[header_off] = typ;
	bw->next = header_off + 4;
	bw->restart_interval = 16;
	bw->entries = 0;
	bw->restart_len = 0;
	bw->last_key.len = 0;
}

uint8_t block_writer_type(struct block_writer *bw)
{
	return bw->buf[bw->header_off];
}

/* Adds the reftable_record to the block. Returns -1 if it does not fit, 0 on
   success. Returns REFTABLE_API_ERROR if attempting to write a record with
   empty key. */
int block_writer_add(struct block_writer *w, struct reftable_record *rec)
{
	struct strbuf empty = STRBUF_INIT;
	struct strbuf last =
		w->entries % w->restart_interval == 0 ? empty : w->last_key;
	struct string_view out = {
		.buf = w->buf + w->next,
		.len = w->block_size - w->next,
	};

	struct string_view start = out;

	int is_restart = 0;
	struct strbuf key = STRBUF_INIT;
	int n = 0;
	int err = -1;

	reftable_record_key(rec, &key);
	if (!key.len) {
		err = REFTABLE_API_ERROR;
		goto done;
	}

	n = reftable_encode_key(&is_restart, out, last, key,
				reftable_record_val_type(rec));
	if (n < 0)
		goto done;
	string_view_consume(&out, n);

	n = reftable_record_encode(rec, out, w->hash_size);
	if (n < 0)
		goto done;
	string_view_consume(&out, n);

	err = block_writer_register_restart(w, start.len - out.len, is_restart,
					    &key);
done:
	strbuf_release(&key);
	return err;
}

int block_writer_finish(struct block_writer *w)
{
	int i;
	for (i = 0; i < w->restart_len; i++) {
		put_be24(w->buf + w->next, w->restarts[i]);
		w->next += 3;
	}

	put_be16(w->buf + w->next, w->restart_len);
	w->next += 2;
	put_be24(w->buf + 1 + w->header_off, w->next);

	if (block_writer_type(w) == BLOCK_TYPE_LOG) {
		int block_header_skip = 4 + w->header_off;
		uLongf src_len = w->next - block_header_skip;
		uLongf dest_cap = src_len * 1.001 + 12;

		uint8_t *compressed = reftable_malloc(dest_cap);
		while (1) {
			uLongf out_dest_len = dest_cap;
			int zresult = compress2(compressed, &out_dest_len,
						w->buf + block_header_skip,
						src_len, 9);
			if (zresult == Z_BUF_ERROR && dest_cap < LONG_MAX) {
				dest_cap *= 2;
				compressed =
					reftable_realloc(compressed, dest_cap);
				if (compressed)
					continue;
			}

			if (Z_OK != zresult) {
				reftable_free(compressed);
				return REFTABLE_ZLIB_ERROR;
			}

			memcpy(w->buf + block_header_skip, compressed,
			       out_dest_len);
			w->next = out_dest_len + block_header_skip;
			reftable_free(compressed);
			break;
		}
	}
	return w->next;
}

uint8_t block_reader_type(struct block_reader *r)
{
	return r->block.data[r->header_off];
}

int block_reader_init(struct block_reader *br, struct reftable_block *block,
		      uint32_t header_off, uint32_t table_block_size,
		      int hash_size)
{
	uint32_t full_block_size = table_block_size;
	uint8_t typ = block->data[header_off];
	uint32_t sz = get_be24(block->data + header_off + 1);
	int err = 0;
	uint16_t restart_count = 0;
	uint32_t restart_start = 0;
	uint8_t *restart_bytes = NULL;
	uint8_t *uncompressed = NULL;

	if (!reftable_is_block_type(typ)) {
		err =  REFTABLE_FORMAT_ERROR;
		goto done;
	}

	if (typ == BLOCK_TYPE_LOG) {
		int block_header_skip = 4 + header_off;
		uLongf dst_len = sz - block_header_skip; /* total size of dest
							    buffer. */
		uLongf src_len = block->len - block_header_skip;
		/* Log blocks specify the *uncompressed* size in their header.
		 */
		uncompressed = reftable_malloc(sz);

		/* Copy over the block header verbatim. It's not compressed. */
		memcpy(uncompressed, block->data, block_header_skip);

		/* Uncompress */
		if (Z_OK !=
		    uncompress2(uncompressed + block_header_skip, &dst_len,
				block->data + block_header_skip, &src_len)) {
			err = REFTABLE_ZLIB_ERROR;
			goto done;
		}

		if (dst_len + block_header_skip != sz) {
			err = REFTABLE_FORMAT_ERROR;
			goto done;
		}

		/* We're done with the input data. */
		reftable_block_done(block);
		block->data = uncompressed;
		uncompressed = NULL;
		block->len = sz;
		block->source = malloc_block_source();
		full_block_size = src_len + block_header_skip;
	} else if (full_block_size == 0) {
		full_block_size = sz;
	} else if (sz < full_block_size && sz < block->len &&
		   block->data[sz] != 0) {
		/* If the block is smaller than the full block size, it is
		   padded (data followed by '\0') or the next block is
		   unaligned. */
		full_block_size = sz;
	}

	restart_count = get_be16(block->data + sz - 2);
	restart_start = sz - 2 - 3 * restart_count;
	restart_bytes = block->data + restart_start;

	/* transfer ownership. */
	br->block = *block;
	block->data = NULL;
	block->len = 0;

	br->hash_size = hash_size;
	br->block_len = restart_start;
	br->full_block_size = full_block_size;
	br->header_off = header_off;
	br->restart_count = restart_count;
	br->restart_bytes = restart_bytes;

done:
	reftable_free(uncompressed);
	return err;
}

static uint32_t block_reader_restart_offset(struct block_reader *br, int i)
{
	return get_be24(br->restart_bytes + 3 * i);
}

void block_reader_start(struct block_reader *br, struct block_iter *it)
{
	it->br = br;
	strbuf_reset(&it->last_key);
	it->next_off = br->header_off + 4;
}

struct restart_find_args {
	int error;
	struct strbuf key;
	struct block_reader *r;
};

static int restart_key_less(size_t idx, void *args)
{
	struct restart_find_args *a = args;
	uint32_t off = block_reader_restart_offset(a->r, idx);
	struct string_view in = {
		.buf = a->r->block.data + off,
		.len = a->r->block_len - off,
	};

	/* the restart key is verbatim in the block, so this could avoid the
	   alloc for decoding the key */
	struct strbuf rkey = STRBUF_INIT;
	struct strbuf last_key = STRBUF_INIT;
	uint8_t unused_extra;
	int n = reftable_decode_key(&rkey, &unused_extra, last_key, in);
	int result;
	if (n < 0) {
		a->error = 1;
		return -1;
	}

	result = strbuf_cmp(&a->key, &rkey);
	strbuf_release(&rkey);
	return result;
}

void block_iter_copy_from(struct block_iter *dest, struct block_iter *src)
{
	dest->br = src->br;
	dest->next_off = src->next_off;
	strbuf_reset(&dest->last_key);
	strbuf_addbuf(&dest->last_key, &src->last_key);
}

int block_iter_next(struct block_iter *it, struct reftable_record *rec)
{
	struct string_view in = {
		.buf = it->br->block.data + it->next_off,
		.len = it->br->block_len - it->next_off,
	};
	struct string_view start = in;
	struct strbuf key = STRBUF_INIT;
	uint8_t extra = 0;
	int n = 0;

	if (it->next_off >= it->br->block_len)
		return 1;

	n = reftable_decode_key(&key, &extra, it->last_key, in);
	if (n < 0)
		return -1;

	if (!key.len)
		return REFTABLE_FORMAT_ERROR;

	string_view_consume(&in, n);
	n = reftable_record_decode(rec, key, extra, in, it->br->hash_size);
	if (n < 0)
		return -1;
	string_view_consume(&in, n);

	strbuf_reset(&it->last_key);
	strbuf_addbuf(&it->last_key, &key);
	it->next_off += start.len - in.len;
	strbuf_release(&key);
	return 0;
}

int block_reader_first_key(struct block_reader *br, struct strbuf *key)
{
	struct strbuf empty = STRBUF_INIT;
	int off = br->header_off + 4;
	struct string_view in = {
		.buf = br->block.data + off,
		.len = br->block_len - off,
	};

	uint8_t extra = 0;
	int n = reftable_decode_key(key, &extra, empty, in);
	if (n < 0)
		return n;
	if (!key->len)
		return REFTABLE_FORMAT_ERROR;

	return 0;
}

int block_iter_seek(struct block_iter *it, struct strbuf *want)
{
	return block_reader_seek(it->br, it, want);
}

void block_iter_close(struct block_iter *it)
{
	strbuf_release(&it->last_key);
}

int block_reader_seek(struct block_reader *br, struct block_iter *it,
		      struct strbuf *want)
{
	struct restart_find_args args = {
		.key = *want,
		.r = br,
	};
	struct reftable_record rec = reftable_new_record(block_reader_type(br));
	struct strbuf key = STRBUF_INIT;
	int err = 0;
	struct block_iter next = {
		.last_key = STRBUF_INIT,
	};

	int i = binsearch(br->restart_count, &restart_key_less, &args);
	if (args.error) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	it->br = br;
	if (i > 0) {
		i--;
		it->next_off = block_reader_restart_offset(br, i);
	} else {
		it->next_off = br->header_off + 4;
	}

	/* We're looking for the last entry less/equal than the wanted key, so
	   we have to go one entry too far and then back up.
	*/
	while (1) {
		block_iter_copy_from(&next, it);
		err = block_iter_next(&next, &rec);
		if (err < 0)
			goto done;

		reftable_record_key(&rec, &key);
		if (err > 0 || strbuf_cmp(&key, want) >= 0) {
			err = 0;
			goto done;
		}

		block_iter_copy_from(it, &next);
	}

done:
	strbuf_release(&key);
	strbuf_release(&next.last_key);
	reftable_record_release(&rec);

	return err;
}

void block_writer_release(struct block_writer *bw)
{
	FREE_AND_NULL(bw->restarts);
	strbuf_release(&bw->last_key);
	/* the block is not owned. */
}

void reftable_block_done(struct reftable_block *blockp)
{
	struct reftable_block_source source = blockp->source;
	if (blockp && source.ops)
		source.ops->return_block(source.arg, blockp);
	blockp->data = NULL;
	blockp->len = 0;
	blockp->source.ops = NULL;
	blockp->source.arg = NULL;
}
