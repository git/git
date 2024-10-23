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

/*
 * The virtual function table for implementing generic reftable iterators.
 */
struct reftable_iterator_vtable {
	int (*seek)(void *iter_arg, struct reftable_record *want);
	int (*next)(void *iter_arg, struct reftable_record *rec);
	void (*close)(void *iter_arg);
};

/*
 * Position the iterator at the wanted record such that a call to
 * `iterator_next()` would return that record, if it exists.
 */
int iterator_seek(struct reftable_iterator *it, struct reftable_record *want);

/*
 * Yield the next record and advance the iterator. Returns <0 on error, 0 when
 * a record was yielded, and >0 when the iterator hit an error.
 */
int iterator_next(struct reftable_iterator *it, struct reftable_record *rec);

/*
 * Set up the iterator such that it behaves the same as an iterator with no
 * entries.
 */
void iterator_set_empty(struct reftable_iterator *it);

/* iterator that produces only ref records that point to `oid` */
struct filtering_ref_iterator {
	struct reftable_buf oid;
	struct reftable_iterator it;
};
#define FILTERING_REF_ITERATOR_INIT \
	{                           \
		.oid = REFTABLE_BUF_INIT  \
	}

void iterator_from_filtering_ref_iterator(struct reftable_iterator *,
					  struct filtering_ref_iterator *);

/* iterator that produces only ref records that point to `oid`,
 * but using the object index.
 */
struct indexed_table_ref_iter {
	struct reftable_reader *r;
	struct reftable_buf oid;

	/* mutable */
	uint64_t *offsets;

	/* Points to the next offset to read. */
	int offset_idx;
	int offset_len;
	struct block_reader block_reader;
	struct block_iter cur;
	int is_finished;
};

#define INDEXED_TABLE_REF_ITER_INIT { \
	.cur = BLOCK_ITER_INIT, \
	.oid = REFTABLE_BUF_INIT, \
}

void iterator_from_indexed_table_ref_iter(struct reftable_iterator *it,
					  struct indexed_table_ref_iter *itr);

/* Takes ownership of `offsets` */
int indexed_table_ref_iter_new(struct indexed_table_ref_iter **dest,
			       struct reftable_reader *r, uint8_t *oid,
			       int oid_len, uint64_t *offsets, int offset_len);

#endif
