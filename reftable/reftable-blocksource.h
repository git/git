/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_BLOCKSOURCE_H
#define REFTABLE_BLOCKSOURCE_H

#include <stdint.h>

/* block_source is a generic wrapper for a seekable readable file.
 */
struct reftable_block_source {
	struct reftable_block_source_vtable *ops;
	void *arg;
};

/* a contiguous segment of bytes. It keeps track of its generating block_source
 * so it can return itself into the pool. */
struct reftable_block {
	uint8_t *data;
	int len;
	struct reftable_block_source source;
};

/* block_source_vtable are the operations that make up block_source */
struct reftable_block_source_vtable {
	/* returns the size of a block source */
	uint64_t (*size)(void *source);

	/* reads a segment from the block source. It is an error to read
	   beyond the end of the block */
	int (*read_block)(void *source, struct reftable_block *dest,
			  uint64_t off, uint32_t size);
	/* mark the block as read; may return the data back to malloc */
	void (*return_block)(void *source, struct reftable_block *blockp);

	/* release all resources associated with the block source */
	void (*close)(void *source);
};

/* opens a file on the file system as a block_source */
int reftable_block_source_from_file(struct reftable_block_source *block_src,
				    const char *name);

#endif
