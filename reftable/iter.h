/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef ITER_H
#define ITER_H

#include "system.h"
#include "block.h"
#include "record.h"

#include "reftable-iterator.h"
#include "reftable-generic.h"

/* Returns true for a zeroed out iterator, such as the one returned from
 * iterator_destroy. */
int iterator_is_null(struct reftable_iterator *it);

/* iterator that produces only ref records that point to `oid` */
struct filtering_ref_iterator {
	int double_check;
	struct reftable_table tab;
	struct strbuf oid;
	struct reftable_iterator it;
};
#define FILTERING_REF_ITERATOR_INIT \
	{                           \
		.oid = STRBUF_INIT  \
	}

void iterator_from_filtering_ref_iterator(struct reftable_iterator *,
					  struct filtering_ref_iterator *);

/* iterator that produces only ref records that point to `oid`,
 * but using the object index.
 */
struct indexed_table_ref_iter {
	struct reftable_reader *r;
	struct strbuf oid;

	/* mutable */
	uint64_t *offsets;

	/* Points to the next offset to read. */
	int offset_idx;
	int offset_len;
	struct block_reader block_reader;
	struct block_iter cur;
	int is_finished;
};

#define INDEXED_TABLE_REF_ITER_INIT                                     \
	{                                                               \
		.cur = { .last_key = STRBUF_INIT }, .oid = STRBUF_INIT, \
	}

void iterator_from_indexed_table_ref_iter(struct reftable_iterator *it,
					  struct indexed_table_ref_iter *itr);

/* Takes ownership of `offsets` */
int new_indexed_table_ref_iter(struct indexed_table_ref_iter **dest,
			       struct reftable_reader *r, uint8_t *oid,
			       int oid_len, uint64_t *offsets, int offset_len);

#endif
