/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#ifndef REFTABLE_BLOCK_H
#define REFTABLE_BLOCK_H

#include <stdint.h>

#include "reftable-basics.h"
#include "reftable-blocksource.h"
#include "reftable-iterator.h"

struct z_stream_s;

/*
 * A block part of a reftable. Contains records as well as some metadata
 * describing them.
 */
struct reftable_block {
	/*
	 * Offset of the block header; nonzero for the first block in a
	 * reftable.
	 */
	uint32_t header_off;

	/* The memory block. */
	struct reftable_block_data block_data;
	uint32_t hash_size;

	/* Uncompressed data for log entries. */
	struct z_stream_s *zstream;
	unsigned char *uncompressed_data;
	size_t uncompressed_cap;

	/*
	 * Restart point data. Restart points are located after the block's
	 * record data.
	 */
	uint16_t restart_count;
	uint32_t restart_off;

	/*
	 * Size of the data in the file. For log blocks, this is the compressed
	 * size.
	 */
	uint32_t full_block_size;
	uint8_t block_type;
};

/* Initialize a reftable block from the given block source. */
int reftable_block_init(struct reftable_block *b,
			struct reftable_block_source *source,
			uint32_t offset, uint32_t header_size,
			uint32_t table_block_size, uint32_t hash_size,
			uint8_t want_type);

/* Release resources allocated by the block. */
void reftable_block_release(struct reftable_block *b);

/* Initialize a generic record iterator from the given block. */
int reftable_block_init_iterator(const struct reftable_block *b,
				 struct reftable_iterator *it);

/* Returns the block type (eg. 'r' for refs). */
uint8_t reftable_block_type(const struct reftable_block *b);

/* Decodes the first key in the block. */
int reftable_block_first_key(const struct reftable_block *b, struct reftable_buf *key);

#endif /* REFTABLE_BLOCK_H */
