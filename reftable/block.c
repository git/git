/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#include "block.h"

#include "blocksource.h"
#include "constants.h"
#include "iter.h"
#include "record.h"
#include "reftable-error.h"
#include "system.h"

size_t header_size(int version)
{
	switch (version) {
	case 1:
		return 24;
	case 2:
		return 28;
	}
	abort();
}

size_t footer_size(int version)
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
					 int is_restart, struct reftable_buf *key)
{
	uint32_t rlen;
	int err;

	rlen = w->restart_len;
	if (rlen >= MAX_RESTARTS)
		is_restart = 0;

	if (is_restart)
		rlen++;
	if (2 + 3 * rlen + n > w->block_size - w->next)
		return REFTABLE_ENTRY_TOO_BIG_ERROR;
	if (is_restart) {
		REFTABLE_ALLOC_GROW_OR_NULL(w->restarts, w->restart_len + 1,
					    w->restart_cap);
		if (!w->restarts)
			return REFTABLE_OUT_OF_MEMORY_ERROR;
		w->restarts[w->restart_len++] = w->next;
	}

	w->next += n;

	reftable_buf_reset(&w->last_key);
	err = reftable_buf_add(&w->last_key, key->buf, key->len);
	if (err < 0)
		return err;

	w->entries++;
	return 0;
}

int block_writer_init(struct block_writer *bw, uint8_t typ, uint8_t *block,
		      uint32_t block_size, uint32_t header_off, uint32_t hash_size)
{
	bw->block = block;
	bw->hash_size = hash_size;
	bw->block_size = block_size;
	bw->header_off = header_off;
	bw->block[header_off] = typ;
	bw->next = header_off + 4;
	bw->restart_interval = 16;
	bw->entries = 0;
	bw->restart_len = 0;
	bw->last_key.len = 0;
	if (!bw->zstream) {
		REFTABLE_CALLOC_ARRAY(bw->zstream, 1);
		if (!bw->zstream)
			return REFTABLE_OUT_OF_MEMORY_ERROR;
		deflateInit(bw->zstream, 9);
	}

	return 0;
}

uint8_t block_writer_type(struct block_writer *bw)
{
	return bw->block[bw->header_off];
}

/*
 * Adds the reftable_record to the block. Returns 0 on success and
 * appropriate error codes on failure.
 */
int block_writer_add(struct block_writer *w, struct reftable_record *rec)
{
	struct reftable_buf empty = REFTABLE_BUF_INIT;
	struct reftable_buf last =
		w->entries % w->restart_interval == 0 ? empty : w->last_key;
	struct string_view out = {
		.buf = w->block + w->next,
		.len = w->block_size - w->next,
	};
	struct string_view start = out;
	int is_restart = 0;
	int n = 0;
	int err;

	err = reftable_record_key(rec, &w->scratch);
	if (err < 0)
		goto done;

	if (!w->scratch.len) {
		err = REFTABLE_API_ERROR;
		goto done;
	}

	n = reftable_encode_key(&is_restart, out, last, w->scratch,
				reftable_record_val_type(rec));
	if (n < 0) {
		err = n;
		goto done;
	}
	string_view_consume(&out, n);

	n = reftable_record_encode(rec, out, w->hash_size);
	if (n < 0) {
		err = n;
		goto done;
	}
	string_view_consume(&out, n);

	err = block_writer_register_restart(w, start.len - out.len, is_restart,
					    &w->scratch);
done:
	return err;
}

int block_writer_finish(struct block_writer *w)
{
	for (uint32_t i = 0; i < w->restart_len; i++) {
		reftable_put_be24(w->block + w->next, w->restarts[i]);
		w->next += 3;
	}

	reftable_put_be16(w->block + w->next, w->restart_len);
	w->next += 2;
	reftable_put_be24(w->block + 1 + w->header_off, w->next);

	/*
	 * Log records are stored zlib-compressed. Note that the compression
	 * also spans over the restart points we have just written.
	 */
	if (block_writer_type(w) == REFTABLE_BLOCK_TYPE_LOG) {
		int block_header_skip = 4 + w->header_off;
		uLongf src_len = w->next - block_header_skip, compressed_len;
		int ret;

		ret = deflateReset(w->zstream);
		if (ret != Z_OK)
			return REFTABLE_ZLIB_ERROR;

		/*
		 * Precompute the upper bound of how many bytes the compressed
		 * data may end up with. Combined with `Z_FINISH`, `deflate()`
		 * is guaranteed to return `Z_STREAM_END`.
		 */
		compressed_len = deflateBound(w->zstream, src_len);
		REFTABLE_ALLOC_GROW_OR_NULL(w->compressed, compressed_len,
					    w->compressed_cap);
		if (!w->compressed) {
			ret = REFTABLE_OUT_OF_MEMORY_ERROR;
			return ret;
		}

		w->zstream->next_out = w->compressed;
		w->zstream->avail_out = compressed_len;
		w->zstream->next_in = w->block + block_header_skip;
		w->zstream->avail_in = src_len;

		/*
		 * We want to perform all decompression in a single step, which
		 * is why we can pass Z_FINISH here. As we have precomputed the
		 * deflated buffer's size via `deflateBound()` this function is
		 * guaranteed to succeed according to the zlib documentation.
		 */
		ret = deflate(w->zstream, Z_FINISH);
		if (ret != Z_STREAM_END)
			return REFTABLE_ZLIB_ERROR;

		/*
		 * Overwrite the uncompressed data we have already written and
		 * adjust the `next` pointer to point right after the
		 * compressed data.
		 */
		memcpy(w->block + block_header_skip, w->compressed,
		       w->zstream->total_out);
		w->next = w->zstream->total_out + block_header_skip;
	}

	return w->next;
}

static int read_block(struct reftable_block_source *source,
		      struct reftable_block_data *dest, uint64_t off,
		      uint32_t sz)
{
	size_t size = block_source_size(source);
	block_source_release_data(dest);
	if (off >= size)
		return 0;
	if (off + sz > size)
		sz = size - off;
	return block_source_read_data(source, dest, off, sz);
}

int reftable_block_init(struct reftable_block *block,
			struct reftable_block_source *source,
			uint32_t offset, uint32_t header_size,
			uint32_t table_block_size, uint32_t hash_size,
			uint8_t want_type)
{
	uint32_t guess_block_size = table_block_size ?
		table_block_size : DEFAULT_BLOCK_SIZE;
	uint32_t full_block_size = table_block_size;
	uint16_t restart_count;
	uint32_t restart_off;
	uint32_t block_size;
	uint8_t block_type;
	int err;

	err = read_block(source, &block->block_data, offset, guess_block_size);
	if (err < 0)
		goto done;

	block_type = block->block_data.data[header_size];
	if (!reftable_is_block_type(block_type)) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}
	if (want_type != REFTABLE_BLOCK_TYPE_ANY && block_type != want_type) {
		err = 1;
		goto done;
	}

	block_size = reftable_get_be24(block->block_data.data + header_size + 1);
	if (block_size > guess_block_size) {
		err = read_block(source, &block->block_data, offset, block_size);
		if (err < 0)
			goto done;
	}

	if (block_type == REFTABLE_BLOCK_TYPE_LOG) {
		uint32_t block_header_skip = 4 + header_size;
		uLong dst_len = block_size - block_header_skip;
		uLong src_len = block->block_data.len - block_header_skip;

		/* Log blocks specify the *uncompressed* size in their header. */
		REFTABLE_ALLOC_GROW_OR_NULL(block->uncompressed_data, block_size,
					    block->uncompressed_cap);
		if (!block->uncompressed_data) {
			err = REFTABLE_OUT_OF_MEMORY_ERROR;
			goto done;
		}

		/* Copy over the block header verbatim. It's not compressed. */
		memcpy(block->uncompressed_data, block->block_data.data, block_header_skip);

		if (!block->zstream) {
			REFTABLE_CALLOC_ARRAY(block->zstream, 1);
			if (!block->zstream) {
				err = REFTABLE_OUT_OF_MEMORY_ERROR;
				goto done;
			}

			err = inflateInit(block->zstream);
		} else {
			err = inflateReset(block->zstream);
		}
		if (err != Z_OK) {
			err = REFTABLE_ZLIB_ERROR;
			goto done;
		}

		block->zstream->next_in = block->block_data.data + block_header_skip;
		block->zstream->avail_in = src_len;
		block->zstream->next_out = block->uncompressed_data + block_header_skip;
		block->zstream->avail_out = dst_len;

		/*
		 * We know both input as well as output size, and we know that
		 * the sizes should never be bigger than `uInt_MAX` because
		 * blocks can at most be 16MB large. We can thus use `Z_FINISH`
		 * here to instruct zlib to inflate the data in one go, which
		 * is more efficient than using `Z_NO_FLUSH`.
		 */
		err = inflate(block->zstream, Z_FINISH);
		if (err != Z_STREAM_END) {
			err = REFTABLE_ZLIB_ERROR;
			goto done;
		}
		err = 0;

		if (block->zstream->total_out + block_header_skip != block_size) {
			err = REFTABLE_FORMAT_ERROR;
			goto done;
		}

		/* We're done with the input data. */
		block_source_release_data(&block->block_data);
		block->block_data.data = block->uncompressed_data;
		block->block_data.len = block_size;
		full_block_size = src_len + block_header_skip - block->zstream->avail_in;
	} else if (full_block_size == 0) {
		full_block_size = block_size;
	} else if (block_size < full_block_size && block_size < block->block_data.len &&
		   block->block_data.data[block_size] != 0) {
		/* If the block is smaller than the full block size, it is
		   padded (data followed by '\0') or the next block is
		   unaligned. */
		full_block_size = block_size;
	}

	restart_count = reftable_get_be16(block->block_data.data + block_size - 2);
	restart_off = block_size - 2 - 3 * restart_count;

	block->block_type = block_type;
	block->hash_size = hash_size;
	block->restart_off = restart_off;
	block->full_block_size = full_block_size;
	block->header_off = header_size;
	block->restart_count = restart_count;

	err = 0;

done:
	if (err < 0)
		reftable_block_release(block);
	return err;
}

void reftable_block_release(struct reftable_block *block)
{
	inflateEnd(block->zstream);
	reftable_free(block->zstream);
	reftable_free(block->uncompressed_data);
	block_source_release_data(&block->block_data);
	memset(block, 0, sizeof(*block));
}

uint8_t reftable_block_type(const struct reftable_block *b)
{
	return b->block_data.data[b->header_off];
}

int reftable_block_first_key(const struct reftable_block *block, struct reftable_buf *key)
{
	int off = block->header_off + 4, n;
	struct string_view in = {
		.buf = block->block_data.data + off,
		.len = block->restart_off - off,
	};
	uint8_t extra = 0;

	reftable_buf_reset(key);

	n = reftable_decode_key(key, &extra, in);
	if (n < 0)
		return n;
	if (!key->len)
		return REFTABLE_FORMAT_ERROR;

	return 0;
}

static uint32_t block_restart_offset(const struct reftable_block *b, size_t idx)
{
	return reftable_get_be24(b->block_data.data + b->restart_off + 3 * idx);
}

void block_iter_init(struct block_iter *it, const struct reftable_block *block)
{
	it->block = block;
	block_iter_seek_start(it);
}

void block_iter_seek_start(struct block_iter *it)
{
	reftable_buf_reset(&it->last_key);
	it->next_off = it->block->header_off + 4;
}

struct restart_needle_less_args {
	int error;
	struct reftable_buf needle;
	const struct reftable_block *block;
};

static int restart_needle_less(size_t idx, void *_args)
{
	struct restart_needle_less_args *args = _args;
	uint32_t off = block_restart_offset(args->block, idx);
	struct string_view in = {
		.buf = args->block->block_data.data + off,
		.len = args->block->restart_off - off,
	};
	uint64_t prefix_len, suffix_len;
	uint8_t extra;
	int n;

	/*
	 * Records at restart points are stored without prefix compression, so
	 * there is no need to fully decode the record key here. This removes
	 * the need for allocating memory.
	 */
	n = reftable_decode_keylen(in, &prefix_len, &suffix_len, &extra);
	if (n < 0 || prefix_len) {
		args->error = 1;
		return -1;
	}

	string_view_consume(&in, n);
	if (suffix_len > in.len) {
		args->error = 1;
		return -1;
	}

	n = memcmp(args->needle.buf, in.buf,
		   args->needle.len < suffix_len ? args->needle.len : suffix_len);
	if (n)
		return n < 0;
	return args->needle.len < suffix_len;
}

int block_iter_next(struct block_iter *it, struct reftable_record *rec)
{
	struct string_view in = {
		.buf = (unsigned char *) it->block->block_data.data + it->next_off,
		.len = it->block->restart_off - it->next_off,
	};
	struct string_view start = in;
	uint8_t extra = 0;
	int n = 0;

	if (it->next_off >= it->block->restart_off)
		return 1;

	n = reftable_decode_key(&it->last_key, &extra, in);
	if (n < 0)
		return -1;
	if (!it->last_key.len)
		return REFTABLE_FORMAT_ERROR;

	string_view_consume(&in, n);
	n = reftable_record_decode(rec, it->last_key, extra, in, it->block->hash_size,
				   &it->scratch);
	if (n < 0)
		return -1;
	string_view_consume(&in, n);

	it->next_off += start.len - in.len;
	return 0;
}

void block_iter_reset(struct block_iter *it)
{
	reftable_buf_reset(&it->last_key);
	it->next_off = 0;
	it->block = NULL;
}

void block_iter_close(struct block_iter *it)
{
	reftable_buf_release(&it->last_key);
	reftable_buf_release(&it->scratch);
}

int block_iter_seek_key(struct block_iter *it, struct reftable_buf *want)
{
	struct restart_needle_less_args args = {
		.needle = *want,
		.block = it->block,
	};
	struct reftable_record rec;
	int err = 0;
	size_t i;

	/*
	 * Perform a binary search over the block's restart points, which
	 * avoids doing a linear scan over the whole block. Like this, we
	 * identify the section of the block that should contain our key.
	 *
	 * Note that we explicitly search for the first restart point _greater_
	 * than the sought-after record, not _greater or equal_ to it. In case
	 * the sought-after record is located directly at the restart point we
	 * would otherwise start doing the linear search at the preceding
	 * restart point. While that works alright, we would end up scanning
	 * too many record.
	 */
	i = binsearch(it->block->restart_count, &restart_needle_less, &args);
	if (args.error) {
		err = REFTABLE_FORMAT_ERROR;
		goto done;
	}

	/*
	 * Now there are multiple cases:
	 *
	 *   - `i == 0`: The wanted record is smaller than the record found at
	 *     the first restart point. As the first restart point is the first
	 *     record in the block, our wanted record cannot be located in this
	 *     block at all. We still need to position the iterator so that the
	 *     next call to `block_iter_next()` will yield an end-of-iterator
	 *     signal.
	 *
	 *   - `i == restart_count`: The wanted record was not found at any of
	 *     the restart points. As there is no restart point at the end of
	 *     the section the record may thus be contained in the last block.
	 *
	 *   - `i > 0`: The wanted record must be contained in the section
	 *     before the found restart point. We thus do a linear search
	 *     starting from the preceding restart point.
	 */
	if (i > 0)
		it->next_off = block_restart_offset(it->block, i - 1);
	else
		it->next_off = it->block->header_off + 4;

	err = reftable_record_init(&rec, reftable_block_type(it->block));
	if (err < 0)
		goto done;

	/*
	 * We're looking for the last entry less than the wanted key so that
	 * the next call to `block_reader_next()` would yield the wanted
	 * record. We thus don't want to position our iterator at the sought
	 * after record, but one before. To do so, we have to go one entry too
	 * far and then back up.
	 */
	while (1) {
		size_t prev_off = it->next_off;

		err = block_iter_next(it, &rec);
		if (err < 0)
			goto done;
		if (err > 0) {
			it->next_off = prev_off;
			err = 0;
			goto done;
		}

		err = reftable_record_key(&rec, &it->last_key);
		if (err < 0)
			goto done;

		/*
		 * Check whether the current key is greater or equal to the
		 * sought-after key. In case it is greater we know that the
		 * record does not exist in the block and can thus abort early.
		 * In case it is equal to the sought-after key we have found
		 * the desired record.
		 *
		 * Note that we store the next record's key record directly in
		 * `last_key` without restoring the key of the preceding record
		 * in case we need to go one record back. This is safe to do as
		 * `block_iter_next()` would return the ref whose key is equal
		 * to `last_key` now, and naturally all keys share a prefix
		 * with themselves.
		 */
		if (reftable_buf_cmp(&it->last_key, want) >= 0) {
			it->next_off = prev_off;
			goto done;
		}
	}

done:
	reftable_record_release(&rec);
	return err;
}

static int block_iter_seek_void(void *it, struct reftable_record *want)
{
	struct reftable_buf buf = REFTABLE_BUF_INIT;
	struct block_iter *bi = it;
	int err;

	if (bi->block->block_type != want->type)
		return REFTABLE_API_ERROR;

	err = reftable_record_key(want, &buf);
	if (err < 0)
		goto out;

	err = block_iter_seek_key(it, &buf);
	if (err < 0)
		goto out;

	err = 0;

out:
	reftable_buf_release(&buf);
	return err;
}

static int block_iter_next_void(void *it, struct reftable_record *rec)
{
	return block_iter_next(it, rec);
}

static void block_iter_close_void(void *it)
{
	block_iter_close(it);
}

static struct reftable_iterator_vtable block_iter_vtable = {
	.seek = &block_iter_seek_void,
	.next = &block_iter_next_void,
	.close = &block_iter_close_void,
};

int reftable_block_init_iterator(const struct reftable_block *b,
				 struct reftable_iterator *it)
{
	struct block_iter *bi;

	REFTABLE_CALLOC_ARRAY(bi, 1);
	block_iter_init(bi, b);

	assert(!it->ops);
	it->iter_arg = bi;
	it->ops = &block_iter_vtable;

	return 0;
}

void block_writer_release(struct block_writer *bw)
{
	deflateEnd(bw->zstream);
	REFTABLE_FREE_AND_NULL(bw->zstream);
	REFTABLE_FREE_AND_NULL(bw->restarts);
	REFTABLE_FREE_AND_NULL(bw->compressed);
	reftable_buf_release(&bw->scratch);
	reftable_buf_release(&bw->last_key);
	/* the block is not owned. */
}
