/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef WRITER_H
#define WRITER_H

#include "basics.h"
#include "block.h"
#include "tree.h"
#include "reftable-writer.h"

struct reftable_writer {
	ssize_t (*write)(void *, const void *, size_t);
	int (*flush)(void *);
	void *write_arg;
	int pending_padding;
	struct reftable_buf last_key;
	/* Scratch buffer used to avoid allocations. */
	struct reftable_buf scratch;

	/* offset of next block to write. */
	uint64_t next;
	uint64_t min_update_index, max_update_index;
	struct reftable_write_options opts;

	/* memory buffer for writing */
	uint8_t *block;

	/* writer for the current section. NULL or points to
	 * block_writer_data */
	struct block_writer *block_writer;

	struct block_writer block_writer_data;

	/* pending index records for the current section */
	struct reftable_index_record *index;
	size_t index_len;
	size_t index_cap;

	/*
	 * tree for use with tsearch; used to populate the 'o' inverse OID
	 * map */
	struct tree_node *obj_index_tree;

	struct reftable_stats stats;
};

#endif
