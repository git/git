/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_MERGED_H
#define REFTABLE_MERGED_H

#include "reftable-iterator.h"

/*
 * Merged tables
 *
 * A ref database kept in a sequence of table files. The merged_table presents a
 * unified view to reading (seeking, iterating) a sequence of immutable tables.
 *
 * The merged tables are on purpose kept disconnected from their actual storage
 * (eg. files on disk), because it is useful to merge tables aren't files. For
 * example, the per-workspace and global ref namespace can be implemented as a
 * merged table of two stacks of file-backed reftables.
 */

/* A merged table is implements seeking/iterating over a stack of tables. */
struct reftable_merged_table;

/* A generic reftable; see below. */
struct reftable_table;

/* reftable_new_merged_table creates a new merged table. It takes ownership of
   the stack array.
*/
int reftable_new_merged_table(struct reftable_merged_table **dest,
			      struct reftable_table *stack, size_t n,
			      uint32_t hash_id);

/* returns an iterator positioned just before 'name' */
int reftable_merged_table_seek_ref(struct reftable_merged_table *mt,
				   struct reftable_iterator *it,
				   const char *name);

/* returns an iterator for log entry, at given update_index */
int reftable_merged_table_seek_log_at(struct reftable_merged_table *mt,
				      struct reftable_iterator *it,
				      const char *name, uint64_t update_index);

/* like reftable_merged_table_seek_log_at but look for the newest entry. */
int reftable_merged_table_seek_log(struct reftable_merged_table *mt,
				   struct reftable_iterator *it,
				   const char *name);

/* returns the max update_index covered by this merged table. */
uint64_t
reftable_merged_table_max_update_index(struct reftable_merged_table *mt);

/* returns the min update_index covered by this merged table. */
uint64_t
reftable_merged_table_min_update_index(struct reftable_merged_table *mt);

/* releases memory for the merged_table */
void reftable_merged_table_free(struct reftable_merged_table *m);

/* return the hash ID of the merged table. */
uint32_t reftable_merged_table_hash_id(struct reftable_merged_table *m);

/* create a generic table from reftable_merged_table */
void reftable_table_from_merged_table(struct reftable_table *tab,
				      struct reftable_merged_table *table);

#endif
