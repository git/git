/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef ITER_H
#define ITER_H

#include "block.h"
#include "record.h"
#include "slice.h"

struct reftable_iterator_vtable {
	int (*next)(void *iter_arg, struct record rec);
	void (*close)(void *iter_arg);
};

void iterator_set_empty(struct reftable_iterator *it);
int iterator_next(struct reftable_iterator it, struct record rec);
bool iterator_is_null(struct reftable_iterator it);

struct filtering_ref_iterator {
	bool double_check;
	struct reftable_reader *r;
	struct slice oid;
	struct reftable_iterator it;
};

void iterator_from_filtering_ref_iterator(struct reftable_iterator *,
					  struct filtering_ref_iterator *);

struct indexed_table_ref_iter {
	struct reftable_reader *r;
	struct slice oid;

	/* mutable */
	uint64_t *offsets;

	/* Points to the next offset to read. */
	int offset_idx;
	int offset_len;
	struct reftable_block_reader block_reader;
	struct reftable_block_iter cur;
	bool finished;
};

void iterator_from_indexed_table_ref_iter(struct reftable_iterator *it,
					  struct indexed_table_ref_iter *itr);
int new_indexed_table_ref_iter(struct indexed_table_ref_iter **dest,
			       struct reftable_reader *r, byte *oid,
			       int oid_len, uint64_t *offsets, int offset_len);

#endif
