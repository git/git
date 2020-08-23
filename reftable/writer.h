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
#include "reftable.h"
#include "slice.h"
#include "tree.h"

struct writer {
	int (*write)(void *, byte *, int);
	void *write_arg;
	int pending_padding;
	int hash_size;
	struct slice last_key;

	uint64_t next;
	uint64_t min_update_index, max_update_index;
	struct write_options opts;

	byte *block;
	struct block_writer *block_writer;
	struct block_writer block_writer_data;
	struct index_record *index;
	int index_len;
	int index_cap;

	/* tree for use with tsearch */
	struct tree_node *obj_index_tree;

	struct stats stats;
};

int writer_flush_block(struct writer *w);
void writer_clear_index(struct writer *w);
int writer_finish_public_section(struct writer *w);

#endif
