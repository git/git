/*
  Copyright 2020 Google LLC

  Use of this source code is governed by a BSD-style
  license that can be found in the LICENSE file or at
  https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_READER_H
#define REFTABLE_READER_H

#include "reftable-iterator.h"
#include "reftable-blocksource.h"

/*
 * Reading single tables
 *
 * The follow routines are for reading single files. For an
 * application-level interface, skip ahead to struct
 * reftable_merged_table and struct reftable_stack.
 */

/* The reader struct is a handle to an open reftable file. */
struct reftable_reader;

/* Generic table. */
struct reftable_table;

/* reftable_new_reader opens a reftable for reading. If successful,
 * returns 0 code and sets pp. The name is used for creating a
 * stack. Typically, it is the basename of the file. The block source
 * `src` is owned by the reader, and is closed on calling
 * reftable_reader_destroy(). On error, the block source `src` is
 * closed as well.
 */
int reftable_new_reader(struct reftable_reader **pp,
			struct reftable_block_source *src, const char *name);

/* Initialize a reftable iterator for reading refs. */
void reftable_reader_init_ref_iterator(struct reftable_reader *r,
				       struct reftable_iterator *it);

/* Initialize a reftable iterator for reading logs. */
void reftable_reader_init_log_iterator(struct reftable_reader *r,
				       struct reftable_iterator *it);

/* returns the hash ID used in this table. */
uint32_t reftable_reader_hash_id(struct reftable_reader *r);

/* closes and deallocates a reader. */
void reftable_reader_free(struct reftable_reader *);

/* return an iterator for the refs pointing to `oid`. */
int reftable_reader_refs_for(struct reftable_reader *r,
			     struct reftable_iterator *it, uint8_t *oid);

/* return the max_update_index for a table */
uint64_t reftable_reader_max_update_index(struct reftable_reader *r);

/* return the min_update_index for a table */
uint64_t reftable_reader_min_update_index(struct reftable_reader *r);

/* creates a generic table from a file reader. */
void reftable_table_from_reader(struct reftable_table *tab,
				struct reftable_reader *reader);

/* print table onto stdout for debugging. */
int reftable_reader_print_file(const char *tablename);
/* print blocks onto stdout for debugging. */
int reftable_reader_print_blocks(const char *tablename);

#endif
