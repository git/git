/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#ifndef BLOCK_H
#define BLOCK_H

#include "basics.h"
#include "record.h"
#include "reftable-block.h"
#include "reftable-blocksource.h"

/*
 * Writes reftable blocks. The block_writer is reused across blocks to minimize
 * allocation overhead.
 */
struct block_writer {
	struct z_stream_s *zstream;
	unsigned char *compressed;
	size_t compressed_cap;

	uint8_t *block;
	uint32_t block_size;

	/* Offset of the global header. Nonzero in the first block only. */
	uint32_t header_off;

	/* How often to restart keys. */
	uint16_t restart_interval;
	uint32_t hash_size;

	/* Offset of next uint8_t to write. */
	uint32_t next;
	uint32_t *restarts;
	uint32_t restart_len;
	uint32_t restart_cap;

	struct reftable_buf last_key;
	/* Scratch buffer used to avoid allocations. */
	struct reftable_buf scratch;
	int entries;
};

/*
 * initializes the blockwriter to write `typ` entries, using `block` as temporary
 * storage. `block` is not owned by the block_writer. */
int block_writer_init(struct block_writer *bw, uint8_t typ, uint8_t *block,
		      uint32_t block_size, uint32_t header_off, uint32_t hash_size);

/* returns the block type (eg. 'r' for ref records. */
uint8_t block_writer_type(struct block_writer *bw);

/* Attempts to append the record. Returns 0 on success or error code on failure. */
int block_writer_add(struct block_writer *w, struct reftable_record *rec);

/* appends the key restarts, and compress the block if necessary. */
int block_writer_finish(struct block_writer *w);

/* clears out internally allocated block_writer members. */
void block_writer_release(struct block_writer *bw);

/* Iterator for records contained in a single block. */
struct block_iter {
	/* offset within the block of the next entry to read. */
	uint32_t next_off;
	const struct reftable_block *block;

	/* key for last entry we read. */
	struct reftable_buf last_key;
	struct reftable_buf scratch;
};

#define BLOCK_ITER_INIT { \
	.last_key = REFTABLE_BUF_INIT, \
	.scratch = REFTABLE_BUF_INIT, \
}

/*
 * Initialize the block iterator with the given block. The iterator will be
 * positioned at the first record contained in the block. The block must remain
 * valid until the end of the iterator's lifetime. It is valid to re-initialize
 * iterators multiple times.
 */
void block_iter_init(struct block_iter *it, const struct reftable_block *block);

/* Position the initialized iterator at the first record of its block. */
void block_iter_seek_start(struct block_iter *it);

/*
 * Position the initialized iterator at the desired record key. It is not an
 * error in case the record cannot be found. If so, a subsequent call to
 * `block_iter_next()` will indicate that the iterator is exhausted.
 */
int block_iter_seek_key(struct block_iter *it, struct reftable_buf *want);

/* return < 0 for error, 0 for OK, > 0 for EOF. */
int block_iter_next(struct block_iter *it, struct reftable_record *rec);

/* Reset the block iterator to pristine state without releasing its memory. */
void block_iter_reset(struct block_iter *it);

/* deallocate memory for `it`. The block reader and its block is left intact. */
void block_iter_close(struct block_iter *it);

/* size of file header, depending on format version */
size_t header_size(int version);

/* size of file footer, depending on format version */
size_t footer_size(int version);

#endif
