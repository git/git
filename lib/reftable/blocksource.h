/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#ifndef BLOCKSOURCE_H
#define BLOCKSOURCE_H

#include "system.h"

struct reftable_block_source;
struct reftable_block_data;
struct reftable_buf;

/*
 * Close the block source and the underlying resource. This is a no-op in case
 * the block source is zero-initialized.
 */
void block_source_close(struct reftable_block_source *source);

/*
 * Read a block of length `size` from the source at the given `off`.
 */
ssize_t block_source_read_data(struct reftable_block_source *source,
			       struct reftable_block_data *dest, uint64_t off,
			       uint32_t size);

/*
 * Return the total length of the underlying resource.
 */
uint64_t block_source_size(struct reftable_block_source *source);

/*
 * Return a block to its original source, releasing any resources associated
 * with it.
 */
void block_source_release_data(struct reftable_block_data *data);

/* Create an in-memory block source for reading reftables. */
void block_source_from_buf(struct reftable_block_source *bs,
			   struct reftable_buf *buf);

#endif
