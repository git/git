/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef MERGED_H
#define MERGED_H

#include "system.h"
#include "reftable-basics.h"

struct reftable_merged_table {
	struct reftable_reader **readers;
	size_t readers_len;
	enum reftable_hash hash_id;

	/* If unset, produce deletions. This is useful for compaction. For the
	 * full stack, deletions should be produced. */
	int suppress_deletions;

	uint64_t min;
	uint64_t max;
};

struct reftable_iterator;

int merged_table_init_iter(struct reftable_merged_table *mt,
			   struct reftable_iterator *it,
			   uint8_t typ);

#endif
