/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_H
#define REFTABLE_H

#include <stdint.h>
#include <stddef.h>

void reftable_set_alloc(void *(*malloc)(size_t),
			void *(*realloc)(void *, size_t), void (*free)(void *));

/****************************************************************
 Basic data types

 Reftables store the state of each ref in struct reftable_ref_record, and they
 store a sequence of reflog updates in struct reftable_log_record.
 ****************************************************************/

/* reftable_ref_record holds a ref database entry target_value */
struct reftable_ref_record {
	char *ref_name; /* Name of the ref, malloced. */
	uint64_t update_index; /* Logical timestamp at which this value is
				  written */
	uint8_t *value; /* SHA1, or NULL. malloced. */
	uint8_t *target_value; /* peeled annotated tag, or NULL. malloced. */
	char *target; /* symref, or NULL. malloced. */
};

/* returns whether 'ref' represents a deletion */
int reftable_ref_record_is_deletion(const struct reftable_ref_record *ref);

/* prints a reftable_ref_record onto stdout */
void reftable_ref_record_print(struct reftable_ref_record *ref, int hash_size);

/* frees and nulls all pointer values. */
void reftable_ref_record_clear(struct reftable_ref_record *ref);

/* returns whether two reftable_ref_records are the same */
int reftable_ref_record_equal(struct reftable_ref_record *a,
			      struct reftable_ref_record *b, int hash_size);

/* reftable_log_record holds a reflog entry */
struct reftable_log_record {
	char *ref_name;
	uint64_t update_index; /* logical timestamp of a transactional update.
				*/
	uint8_t *new_hash;
	uint8_t *old_hash;
	char *name;
	char *email;
	uint64_t time;
	int16_t tz_offset;
	char *message;
};

/* returns whether 'ref' represents the deletion of a log record. */
int reftable_log_record_is_deletion(const struct reftable_log_record *log);

/* frees and nulls all pointer values. */
void reftable_log_record_clear(struct reftable_log_record *log);

/* returns whether two records are equal. */
int reftable_log_record_equal(struct reftable_log_record *a,
			      struct reftable_log_record *b, int hash_size);

/* dumps a reftable_log_record on stdout, for debugging/testing. */
void reftable_log_record_print(struct reftable_log_record *log, int hash_size);

/****************************************************************
 Error handling

 Error are signaled with negative integer return values. 0 means success.
 ****************************************************************/

/* different types of errors */
enum reftable_error {
	/* Unexpected file system behavior */
	IO_ERROR = -2,

	/* Format inconsistency on reading data
	 */
	FORMAT_ERROR = -3,

	/* File does not exist. Returned from block_source_from_file(),  because
	   it needs special handling in stack.
	*/
	NOT_EXIST_ERROR = -4,

	/* Trying to write out-of-date data. */
	LOCK_ERROR = -5,

	/* Misuse of the API:
	   - on writing a record with NULL ref_name.
	   - on writing a reftable_ref_record outside the table limits
	   - on writing a ref or log record before the stack's next_update_index
	   - on reading a reftable_ref_record from log iterator, or vice versa.
	*/
	API_ERROR = -6,

	/* Decompression error */
	ZLIB_ERROR = -7,

	/* Wrote a table without blocks. */
	EMPTY_TABLE_ERROR = -8,
};

/* convert the numeric error code to a string. The string should not be
 * deallocated. */
const char *reftable_error_str(int err);

/****************************************************************
 Writing

 Writing single reftables
 ****************************************************************/

/* reftable_write_options sets options for writing a single reftable. */
struct reftable_write_options {
	/* boolean: do not pad out blocks to block size. */
	int unpadded;

	/* the blocksize. Should be less than 2^24. */
	uint32_t block_size;

	/* boolean: do not generate a SHA1 => ref index. */
	int skip_index_objects;

	/* how often to write complete keys in each block. */
	int restart_interval;

	/* 4-byte identifier ("sha1", "s256") of the hash.
	 * Defaults to SHA1 if unset
	 */
	uint32_t hash_id;
};

/* reftable_block_stats holds statistics for a single block type */
struct reftable_block_stats {
	/* total number of entries written */
	int entries;
	/* total number of key restarts */
	int restarts;
	/* total number of blocks */
	int blocks;
	/* total number of index blocks */
	int index_blocks;
	/* depth of the index */
	int max_index_level;

	/* offset of the first block for this type */
	uint64_t offset;
	/* offset of the top level index block for this type, or 0 if not
	 * present */
	uint64_t index_offset;
};

/* stats holds overall statistics for a single reftable */
struct reftable_stats {
	/* total number of blocks written. */
	int blocks;
	/* stats for ref data */
	struct reftable_block_stats ref_stats;
	/* stats for the SHA1 to ref map. */
	struct reftable_block_stats obj_stats;
	/* stats for index blocks */
	struct reftable_block_stats idx_stats;
	/* stats for log blocks */
	struct reftable_block_stats log_stats;

	/* disambiguation length of shortened object IDs. */
	int object_id_len;
};

/* reftable_new_writer creates a new writer */
struct reftable_writer *
reftable_new_writer(int (*writer_func)(void *, uint8_t *, int),
		    void *writer_arg, struct reftable_write_options *opts);

/* write to a file descriptor. fdp should be an int* pointing to the fd. */
int reftable_fd_write(void *fdp, uint8_t *data, int size);

/* Set the range of update indices for the records we will add.  When
   writing a table into a stack, the min should be at least
   reftable_stack_next_update_index(), or API_ERROR is returned.

   For transactional updates, typically min==max. When converting an existing
   ref database into a single reftable, this would be a range of update-index
   timestamps.
 */
void reftable_writer_set_limits(struct reftable_writer *w, uint64_t min,
				uint64_t max);

/* adds a reftable_ref_record. Must be called in ascending
   order. The update_index must be within the limits set by
   reftable_writer_set_limits(), or API_ERROR is returned.

   It is an error to write a ref record after a log record.
 */
int reftable_writer_add_ref(struct reftable_writer *w,
			    struct reftable_ref_record *ref);

/* Convenience function to add multiple refs. Will sort the refs by
   name before adding. */
int reftable_writer_add_refs(struct reftable_writer *w,
			     struct reftable_ref_record *refs, int n);

/* adds a reftable_log_record. Must be called in ascending order (with more
   recent log entries first.)
 */
int reftable_writer_add_log(struct reftable_writer *w,
			    struct reftable_log_record *log);

/* Convenience function to add multiple logs. Will sort the records by
   key before adding. */
int reftable_writer_add_logs(struct reftable_writer *w,
			     struct reftable_log_record *logs, int n);

/* reftable_writer_close finalizes the reftable. The writer is retained so
 * statistics can be inspected. */
int reftable_writer_close(struct reftable_writer *w);

/* writer_stats returns the statistics on the reftable being written.

   This struct becomes invalid when the writer is freed.
 */
const struct reftable_stats *writer_stats(struct reftable_writer *w);

/* reftable_writer_free deallocates memory for the writer */
void reftable_writer_free(struct reftable_writer *w);

/****************************************************************
 * ITERATING
 ****************************************************************/

/* iterator is the generic interface for walking over data stored in a
   reftable. It is generally passed around by value.
*/
struct reftable_iterator {
	struct reftable_iterator_vtable *ops;
	void *iter_arg;
};

/* reads the next reftable_ref_record. Returns < 0 for error, 0 for OK and > 0:
   end of iteration.
*/
int reftable_iterator_next_ref(struct reftable_iterator it,
			       struct reftable_ref_record *ref);

/* reads the next reftable_log_record. Returns < 0 for error, 0 for OK and > 0:
   end of iteration.
*/
int reftable_iterator_next_log(struct reftable_iterator it,
			       struct reftable_log_record *log);

/* releases resources associated with an iterator. */
void reftable_iterator_destroy(struct reftable_iterator *it);

/****************************************************************
 Reading single tables

 The follow routines are for reading single files. For an application-level
 interface, skip ahead to struct reftable_merged_table and struct
 reftable_stack.
 ****************************************************************/

/* block_source is a generic wrapper for a seekable readable file.
   It is generally passed around by value.
 */
struct reftable_block_source {
	struct reftable_block_source_vtable *ops;
	void *arg;
};

/* a contiguous segment of bytes. It keeps track of its generating block_source
   so it can return itself into the pool.
*/
struct reftable_block {
	uint8_t *data;
	int len;
	struct reftable_block_source source;
};

/* block_source_vtable are the operations that make up block_source */
struct reftable_block_source_vtable {
	/* returns the size of a block source */
	uint64_t (*size)(void *source);

	/* reads a segment from the block source. It is an error to read
	   beyond the end of the block */
	int (*read_block)(void *source, struct reftable_block *dest,
			  uint64_t off, uint32_t size);
	/* mark the block as read; may return the data back to malloc */
	void (*return_block)(void *source, struct reftable_block *blockp);

	/* release all resources associated with the block source */
	void (*close)(void *source);
};

/* opens a file on the file system as a block_source */
int reftable_block_source_from_file(struct reftable_block_source *block_src,
				    const char *name);

/* The reader struct is a handle to an open reftable file. */
struct reftable_reader;

/* reftable_new_reader opens a reftable for reading. If successful, returns 0
 * code and sets pp.  The name is used for creating a
 * stack. Typically, it is the basename of the file.
 */
int reftable_new_reader(struct reftable_reader **pp,
			struct reftable_block_source, const char *name);

/* reftable_reader_seek_ref returns an iterator where 'name' would be inserted
   in the table.  To seek to the start of the table, use name = "".

   example:

   struct reftable_reader *r = NULL;
   int err = reftable_new_reader(&r, src, "filename");
   if (err < 0) { ... }
   struct reftable_iterator it  = {0};
   err = reftable_reader_seek_ref(r, &it, "refs/heads/master");
   if (err < 0) { ... }
   struct reftable_ref_record ref  = {0};
   while (1) {
     err = reftable_iterator_next_ref(it, &ref);
     if (err > 0) {
       break;
     }
     if (err < 0) {
       ..error handling..
     }
     ..found..
   }
   reftable_iterator_destroy(&it);
   reftable_ref_record_clear(&ref);
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

/* return an iterator for the refs pointing to oid */
int reftable_reader_refs_for(struct reftable_reader *r,
			     struct reftable_iterator *it, uint8_t *oid,
			     int oid_len);

/* return the max_update_index for a table */
uint64_t reftable_reader_max_update_index(struct reftable_reader *r);

/* return the min_update_index for a table */
uint64_t reftable_reader_min_update_index(struct reftable_reader *r);

/****************************************************************
 Merged tables

 A ref database kept in a sequence of table files. The merged_table presents a
 unified view to reading (seeking, iterating) a sequence of immutable tables.
 ****************************************************************/

/* A merged table is implements seeking/iterating over a stack of tables. */
struct reftable_merged_table;

/* reftable_new_merged_table creates a new merged table. It takes ownership of
   the stack array.
*/
int reftable_new_merged_table(struct reftable_merged_table **dest,
			      struct reftable_reader **stack, int n,
			      uint32_t hash_id);

/* returns the hash id used in this merged table. */
uint32_t reftable_merged_table_hash_id(struct reftable_merged_table *mt);

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

/* closes readers for the merged tables */
void reftable_merged_table_close(struct reftable_merged_table *mt);

/* releases memory for the merged_table */
void reftable_merged_table_free(struct reftable_merged_table *m);

/****************************************************************
 Mutable ref database

 The stack presents an interface to a mutable sequence of reftables.
 ****************************************************************/

/* a stack is a stack of reftables, which can be mutated by pushing a table to
 * the top of the stack */
struct reftable_stack;

/* open a new reftable stack. The tables along with the table list will be
   stored in 'dir'. Typically, this should be .git/reftables.
*/
int reftable_new_stack(struct reftable_stack **dest, const char *dir,
		       struct reftable_write_options config);

/* returns the update_index at which a next table should be written. */
uint64_t reftable_stack_next_update_index(struct reftable_stack *st);

/* holds a transaction to add tables at the top of a stack. */
struct reftable_addition;

/*
  returns a new transaction to add reftables to the given stack. As a side
  effect, the ref database is locked.
*/ 
int reftable_stack_new_addition(struct reftable_addition **dest, struct reftable_stack *st);

/* Adds a reftable to transaction. */ 
int reftable_addition_add(struct reftable_addition *add,
                          int (*write_table)(struct reftable_writer *wr, void *arg),
                          void *arg);

/* Commits the transaction, releasing the lock. */
int reftable_addition_commit(struct reftable_addition *add);

/* Release all non-committed data from the transaction; releases the lock if held. */
void reftable_addition_close(struct reftable_addition *add);

/* add a new table to the stack. The write_table function must call
   reftable_writer_set_limits, add refs and return an error value. */
int reftable_stack_add(struct reftable_stack *st,
		       int (*write_table)(struct reftable_writer *wr,
					  void *write_arg),
		       void *write_arg);

/* returns the merged_table for seeking. This table is valid until the
   next write or reload, and should not be closed or deleted.
*/
struct reftable_merged_table *
reftable_stack_merged_table(struct reftable_stack *st);

/* frees all resources associated with the stack. */
void reftable_stack_destroy(struct reftable_stack *st);

/* reloads the stack if necessary. */
int reftable_stack_reload(struct reftable_stack *st);

/* Policy for expiring reflog entries. */
struct reftable_log_expiry_config {
	/* Drop entries older than this timestamp */
	uint64_t time;

	/* Drop older entries */
	uint64_t min_update_index;
};

/* compacts all reftables into a giant table. Expire reflog entries if config is
 * non-NULL */
int reftable_stack_compact_all(struct reftable_stack *st,
			       struct reftable_log_expiry_config *config);

/* heuristically compact unbalanced table stack. */
int reftable_stack_auto_compact(struct reftable_stack *st);

/* convenience function to read a single ref. Returns < 0 for error, 0
   for success, and 1 if ref not found. */
int reftable_stack_read_ref(struct reftable_stack *st, const char *refname,
			    struct reftable_ref_record *ref);

/* convenience function to read a single log. Returns < 0 for error, 0
   for success, and 1 if ref not found. */
int reftable_stack_read_log(struct reftable_stack *st, const char *refname,
			    struct reftable_log_record *log);

/* statistics on past compactions. */
struct reftable_compaction_stats {
	uint64_t bytes; /* total number of bytes written */
	int attempts; /* how often we tried to compact */
	int failures; /* failures happen on concurrent updates */
};

/* return statistics for compaction up till now. */
struct reftable_compaction_stats *
reftable_stack_compaction_stats(struct reftable_stack *st);

#endif
