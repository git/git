/*
 * Copyright 2020 Google LLC
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd
 */

#ifndef REFTABLE_TABLE_H
#define REFTABLE_TABLE_H

#include "reftable-iterator.h"
#include "reftable-block.h"
#include "reftable-blocksource.h"

/*
 * Reading single tables
 *
 * The follow routines are for reading single files. For an
 * application-level interface, skip ahead to struct
 * reftable_merged_table and struct reftable_stack.
 */

/* Metadata for a block type. */
struct reftable_table_offsets {
	int is_present;
	uint64_t offset;
	uint64_t index_offset;
};

/* The table struct is a handle to an open reftable file. */
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

/* reftable_table_new opens a reftable for reading. If successful,
 * returns 0 code and sets pp. The name is used for creating a
 * stack. Typically, it is the basename of the file. The block source
 * `src` is owned by the table, and is closed on calling
 * reftable_table_destroy(). On error, the block source `src` is
 * closed as well.
 */
int reftable_table_new(struct reftable_table **out,
		       struct reftable_block_source *src, const char *name);

/*
 * Manage the reference count of the reftable table. A newly initialized
 * table starts with a refcount of 1 and will be deleted once the refcount has
 * reached 0.
 *
 * This is required because tables may have longer lifetimes than the stack
 * they belong to. The stack may for example be reloaded while the old tables
 * are still being accessed by an iterator.
 */
void reftable_table_incref(struct reftable_table *table);
void reftable_table_decref(struct reftable_table *table);

/* Initialize a reftable iterator for reading refs. */
int reftable_table_init_ref_iterator(struct reftable_table *t,
				     struct reftable_iterator *it);

/* Initialize a reftable iterator for reading logs. */
int reftable_table_init_log_iterator(struct reftable_table *t,
				     struct reftable_iterator *it);

/* returns the hash ID used in this table. */
enum reftable_hash reftable_table_hash_id(struct reftable_table *t);

/* return an iterator for the refs pointing to `oid`. */
int reftable_table_refs_for(struct reftable_table *t,
			    struct reftable_iterator *it, uint8_t *oid);

/* return the max_update_index for a table */
uint64_t reftable_table_max_update_index(struct reftable_table *t);

/* return the min_update_index for a table */
uint64_t reftable_table_min_update_index(struct reftable_table *t);

/*
 * An iterator that iterates through the blocks contained in a given table.
 */
struct reftable_table_iterator {
	void *iter_arg;
};

int reftable_table_iterator_init(struct reftable_table_iterator *it,
				 struct reftable_table *t);

void reftable_table_iterator_release(struct reftable_table_iterator *it);

int reftable_table_iterator_next(struct reftable_table_iterator *it,
				 const struct reftable_block **out);

#endif
