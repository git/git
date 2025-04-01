/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#ifndef REFTABLE_BLOCKSOURCE_H
#define REFTABLE_BLOCKSOURCE_H

#include <stdint.h>

/*
 * Generic wrapper for a seekable readable file.
 */
struct reftable_block_source {
	struct reftable_block_source_vtable *ops;
	void *arg;
};

/* a contiguous segment of bytes. It keeps track of its generating block_source
 * so it can return itself into the pool. */
struct reftable_block_data {
	uint8_t *data;
	size_t len;
	struct reftable_block_source source;
};

/* block_source_vtable are the operations that make up block_source */
struct reftable_block_source_vtable {
	/* Returns the size of a block source. */
	uint64_t (*size)(void *source);

	/*
	 * Reads a segment from the block source. It is an error to read beyond
	 * the end of the block.
	 */
	ssize_t (*read_data)(void *source, struct reftable_block_data *dest,
			uint64_t off, uint32_t size);

	/* Mark the block as read; may release the data. */
	void (*release_data)(void *source, struct reftable_block_data *data);

	/* Release all resources associated with the block source. */
	void (*close)(void *source);
};

/* opens a file on the file system as a block_source */
int reftable_block_source_from_file(struct reftable_block_source *block_src,
				    const char *name);

#endif
