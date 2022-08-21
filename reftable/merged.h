/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef MERGED_H
#define MERGED_H

#include "pq.h"

struct reftable_merged_table {
	struct reftable_table *stack;
	size_t stack_len;
	uint32_t hash_id;

	/* If unset, produce deletions. This is useful for compaction. For the
	 * full stack, deletions should be produced. */
	int suppress_deletions;

	uint64_t min;
	uint64_t max;
};

struct merged_iter {
	struct reftable_iterator *stack;
	uint32_t hash_id;
	size_t stack_len;
	uint8_t typ;
	int suppress_deletions;
	struct merged_iter_pqueue pq;
};

void merged_table_release(struct reftable_merged_table *mt);

#endif
