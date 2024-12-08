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

struct reftable_reader;

/*
 * reftable_merged_table_new creates a new merged table. The readers must be
 * kept alive as long as the merged table is still in use.
 */
int reftable_merged_table_new(struct reftable_merged_table **dest,
			      struct reftable_reader **readers, size_t n,
			      enum reftable_hash hash_id);

/* Initialize a merged table iterator for reading refs. */
int reftable_merged_table_init_ref_iterator(struct reftable_merged_table *mt,
					    struct reftable_iterator *it);

/* Initialize a merged table iterator for reading logs. */
int reftable_merged_table_init_log_iterator(struct reftable_merged_table *mt,
					    struct reftable_iterator *it);

/* returns the max update_index covered by this merged table. */
uint64_t
reftable_merged_table_max_update_index(struct reftable_merged_table *mt);

/* returns the min update_index covered by this merged table. */
uint64_t
reftable_merged_table_min_update_index(struct reftable_merged_table *mt);

/* releases memory for the merged_table */
void reftable_merged_table_free(struct reftable_merged_table *m);

/* return the hash ID of the merged table. */
enum reftable_hash reftable_merged_table_hash_id(struct reftable_merged_table *m);

#endif
