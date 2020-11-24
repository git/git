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

/* reftable_reader_seek_ref returns an iterator where 'name' would be inserted
   in the table.  To seek to the start of the table, use name = "".

   example:

   struct reftable_reader *r = NULL;
   int err = reftable_new_reader(&r, &src, "filename");
   if (err < 0) { ... }
   struct reftable_iterator it  = {0};
   err = reftable_reader_seek_ref(r, &it, "refs/heads/master");
   if (err < 0) { ... }
   struct reftable_ref_record ref  = {0};
   while (1) {
   err = reftable_iterator_next_ref(&it, &ref);
   if (err > 0) {
   break;
   }
   if (err < 0) {
   ..error handling..
   }
   ..found..
   }
   reftable_iterator_destroy(&it);
   reftable_ref_record_release(&ref);
*/
int reftable_reader_seek_ref(struct reftable_reader *r,
			     struct reftable_iterator *it, const char *name);

/* returns the hash ID used in this table. */
uint32_t reftable_reader_hash_id(struct reftable_reader *r);

/* seek to logs for the given name, older than update_index. To seek to the
   start of the table, use name = "".
*/
int reftable_reader_seek_log_at(struct reftable_reader *r,
				struct reftable_iterator *it, const char *name,
				uint64_t update_index);

/* seek to newest log entry for given name. */
int reftable_reader_seek_log(struct reftable_reader *r,
			     struct reftable_iterator *it, const char *name);

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

#endif
