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
#include "reftable-blocksource.h"

/*
 * Reading single tables
 *
 * The follow routines are for reading single files. For an
 * application-level interface, skip ahead to struct
 * reftable_merged_table and struct reftable_stack.
 */

/* The table struct is a handle to an open reftable file. */
struct reftable_table;

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

/* print blocks onto stdout for debugging. */
int reftable_table_print_blocks(const char *tablename);

#endif
