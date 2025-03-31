/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#ifndef TABLE_H
#define TABLE_H

#include "block.h"
#include "record.h"
#include "reftable-iterator.h"
#include "reftable-table.h"

/* metadata for a block type */
struct reftable_table_offsets {
	int is_present;
	uint64_t offset;
	uint64_t index_offset;
};

/* The state for reading a reftable file. */
struct reftable_table {
	/* for convenience, associate a name with the instance. */
	char *name;
	struct reftable_block_source source;

	/* Size of the file, excluding the footer. */
	uint64_t size;

	/* The hash function used for ref records. */
	enum reftable_hash hash_id;

	uint32_t block_size;
	uint64_t min_update_index;
	uint64_t max_update_index;
	/* Length of the OID keys in the 'o' section */
	int object_id_len;
	int version;

	struct reftable_table_offsets ref_offsets;
	struct reftable_table_offsets obj_offsets;
	struct reftable_table_offsets log_offsets;

	uint64_t refcount;
};

const char *reftable_table_name(struct reftable_table *t);

int table_init_iter(struct reftable_table *t,
		    struct reftable_iterator *it,
		    uint8_t typ);

/*
 * Initialize a block by reading from the given table and offset.
 */
int table_init_block(struct reftable_table *t, struct reftable_block *block,
		     uint64_t next_off, uint8_t want_typ);

#endif
