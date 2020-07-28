/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_H
#define REFTABLE_H

#include "system.h"

typedef uint8_t byte;
typedef byte bool;

/* block_source is a generic wrapper for a seekable readable file.
   It is generally passed around by value.
 */
struct block_source {
	struct block_source_vtable *ops;
	void *arg;
};

/* a contiguous segment of bytes. It keeps track of its generating block_source
   so it can return itself into the pool.
*/
struct block {
	byte *data;
	int len;
	struct block_source source;
};

/* block_source_vtable are the operations that make up block_source */
struct block_source_vtable {
	/* returns the size of a block source */
	uint64_t (*size)(void *source);

	/* reads a segment from the block source. It is an error to read
	   beyond the end of the block */
	int (*read_block)(void *source, struct block *dest, uint64_t off,
			  uint32_t size);
	/* mark the block as read; may return the data back to malloc */
	void (*return_block)(void *source, struct block *blockp);

	/* release all resources associated with the block source */
	void (*close)(void *source);
};

/* opens a file on the file system as a block_source */
int block_source_from_file(struct block_source *block_src, const char *name);

/* write_options sets options for writing a single reftable. */
struct write_options {
	/* do not pad out blocks to block size. */
	bool unpadded;

	/* the blocksize. Should be less than 2^24. */
	uint32_t block_size;

	/* do not generate a SHA1 => ref index. */
	bool skip_index_objects;

	/* how often to write complete keys in each block. */
	int restart_interval;
};

/* ref_record holds a ref database entry target_value */
struct ref_record {
	char *ref_name; /* Name of the ref, malloced. */
	uint64_t update_index; /* Logical timestamp at which this value is
				  written */
	byte *value; /* SHA1, or NULL. malloced. */
	byte *target_value; /* peeled annotated tag, or NULL. malloced. */
	char *target; /* symref, or NULL. malloced. */
};

/* returns whether 'ref' represents a deletion */
bool ref_record_is_deletion(const struct ref_record *ref);

/* prints a ref_record onto stdout */
void ref_record_print(struct ref_record *ref, int hash_size);

/* frees and nulls all pointer values. */
void ref_record_clear(struct ref_record *ref);

/* returns whether two ref_records are the same */
bool ref_record_equal(struct ref_record *a, struct ref_record *b,
		      int hash_size);

/* log_record holds a reflog entry */
struct log_record {
	char *ref_name;
	uint64_t update_index;
	byte *new_hash;
	byte *old_hash;
	char *name;
	char *email;
	uint64_t time;
	int16_t tz_offset;
	char *message;
};

/* returns whether 'ref' represents the deletion of a log record. */
bool log_record_is_deletion(const struct log_record *log);

/* frees and nulls all pointer values. */
void log_record_clear(struct log_record *log);

/* returns whether two records are equal. */
bool log_record_equal(struct log_record *a, struct log_record *b,
		      int hash_size);

void log_record_print(struct log_record *log, int hash_size);

/* iterator is the generic interface for walking over data stored in a
   reftable. It is generally passed around by value.
*/
struct iterator {
	struct iterator_vtable *ops;
	void *iter_arg;
};

/* reads the next ref_record. Returns < 0 for error, 0 for OK and > 0:
   end of iteration.
*/
int iterator_next_ref(struct iterator it, struct ref_record *ref);

/* reads the next log_record. Returns < 0 for error, 0 for OK and > 0:
   end of iteration.
*/
int iterator_next_log(struct iterator it, struct log_record *log);

/* releases resources associated with an iterator. */
void iterator_destroy(struct iterator *it);

/* block_stats holds statistics for a single block type */
struct block_stats {
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
struct stats {
	/* total number of blocks written. */
	int blocks;
	/* stats for ref data */
	struct block_stats ref_stats;
	/* stats for the SHA1 to ref map. */
	struct block_stats obj_stats;
	/* stats for index blocks */
	struct block_stats idx_stats;
	/* stats for log blocks */
	struct block_stats log_stats;

	/* disambiguation length of shortened object IDs. */
	int object_id_len;
};

/* different types of errors */

/* Unexpected file system behavior */
#define IO_ERROR -2

/* Format inconsistency on reading data
 */
#define FORMAT_ERROR -3

/* File does not exist. Returned from block_source_from_file(),  because it
   needs special handling in stack.
*/
#define NOT_EXIST_ERROR -4

/* Trying to write out-of-date data. */
#define LOCK_ERROR -5

/* Misuse of the API:
   - on writing a record with NULL ref_name.
   - on writing a ref_record outside the table limits
   - on writing a ref or log record before the stack's next_update_index
   - on reading a ref_record from log iterator, or vice versa.
 */
#define API_ERROR -6

/* Decompression error */
#define ZLIB_ERROR -7

const char *error_str(int err);

/* new_writer creates a new writer */
struct writer *new_writer(int (*writer_func)(void *, byte *, int),
			  void *writer_arg, struct write_options *opts);

/* write to a file descriptor. fdp should be an int* pointing to the fd. */
int fd_writer(void *fdp, byte *data, int size);

/* Set the range of update indices for the records we will add.  When
   writing a table into a stack, the min should be at least
   stack_next_update_index(), or API_ERROR is returned.
 */
void writer_set_limits(struct writer *w, uint64_t min, uint64_t max);

/* adds a ref_record. Must be called in ascending
   order. The update_index must be within the limits set by
   writer_set_limits(), or API_ERROR is returned.
 */
int writer_add_ref(struct writer *w, struct ref_record *ref);

/* Convenience function to add multiple refs. Will sort the refs by
   name before adding. */
int writer_add_refs(struct writer *w, struct ref_record *refs, int n);

/* adds a log_record. Must be called in ascending order (with more
   recent log entries first.)
 */
int writer_add_log(struct writer *w, struct log_record *log);

/* Convenience function to add multiple logs. Will sort the records by
   key before adding. */
int writer_add_logs(struct writer *w, struct log_record *logs, int n);

/* writer_close finalizes the reftable. The writer is retained so statistics can
 * be inspected. */
int writer_close(struct writer *w);

/* writer_stats returns the statistics on the reftable being written. */
struct stats *writer_stats(struct writer *w);

/* writer_free deallocates memory for the writer */
void writer_free(struct writer *w);

struct reader;

/* new_reader opens a reftable for reading. If successful, returns 0
 * code and sets pp.  The name is used for creating a
 * stack. Typically, it is the basename of the file.
 */
int new_reader(struct reader **pp, struct block_source, const char *name);

/* reader_seek_ref returns an iterator where 'name' would be inserted in the
   table.

   example:

   struct reader *r = NULL;
   int err = new_reader(&r, src, "filename");
   if (err < 0) { ... }
   struct iterator it = {};
   err = reader_seek_ref(r, &it, "refs/heads/master");
   if (err < 0) { ... }
   struct ref_record ref = {};
   while (1) {
     err = iterator_next_ref(it, &ref);
     if (err > 0) {
       break;
     }
     if (err < 0) {
       ..error handling..
     }
     ..found..
   }
   iterator_destroy(&it);
   ref_record_clear(&ref);
 */
int reader_seek_ref(struct reader *r, struct iterator *it, const char *name);

/* seek to logs for the given name, older than update_index. */
int reader_seek_log_at(struct reader *r, struct iterator *it, const char *name,
		       uint64_t update_index);

/* seek to newest log entry for given name. */
int reader_seek_log(struct reader *r, struct iterator *it, const char *name);

/* closes and deallocates a reader. */
void reader_free(struct reader *);

/* return an iterator for the refs pointing to oid */
int reader_refs_for(struct reader *r, struct iterator *it, byte *oid,
		    int oid_len);

/* return the max_update_index for a table */
uint64_t reader_max_update_index(struct reader *r);

/* return the min_update_index for a table */
uint64_t reader_min_update_index(struct reader *r);

/* a merged table is implements seeking/iterating over a stack of tables. */
struct merged_table;

/* new_merged_table creates a new merged table. It takes ownership of the stack
   array.
*/
int new_merged_table(struct merged_table **dest, struct reader **stack, int n);

/* returns an iterator positioned just before 'name' */
int merged_table_seek_ref(struct merged_table *mt, struct iterator *it,
			  const char *name);

/* returns an iterator for log entry, at given update_index */
int merged_table_seek_log_at(struct merged_table *mt, struct iterator *it,
			     const char *name, uint64_t update_index);

/* like merged_table_seek_log_at but look for the newest entry. */
int merged_table_seek_log(struct merged_table *mt, struct iterator *it,
			  const char *name);

/* returns the max update_index covered by this merged table. */
uint64_t merged_max_update_index(struct merged_table *mt);

/* returns the min update_index covered by this merged table. */
uint64_t merged_min_update_index(struct merged_table *mt);

/* closes readers for the merged tables */
void merged_table_close(struct merged_table *mt);

/* releases memory for the merged_table */
void merged_table_free(struct merged_table *m);

/* a stack is a stack of reftables, which can be mutated by pushing a table to
 * the top of the stack */
struct stack;

/* open a new reftable stack. The tables will be stored in 'dir', while the list
   of tables is in 'list_file'. Typically, this should be .git/reftables and
   .git/refs respectively.
*/
int new_stack(struct stack **dest, const char *dir, const char *list_file,
	      struct write_options config);

/* returns the update_index at which a next table should be written. */
uint64_t stack_next_update_index(struct stack *st);

/* add a new table to the stack. The write_table function must call
   writer_set_limits, add refs and return an error value. */
int stack_add(struct stack *st,
	      int (*write_table)(struct writer *wr, void *write_arg),
	      void *write_arg);

/* returns the merged_table for seeking. This table is valid until the
   next write or reload, and should not be closed or deleted.
*/
struct merged_table *stack_merged_table(struct stack *st);

/* frees all resources associated with the stack. */
void stack_destroy(struct stack *st);

/* reloads the stack if necessary. */
int stack_reload(struct stack *st);

/* Policy for expiring reflog entries. */
struct log_expiry_config {
	/* Drop entries older than this timestamp */
	uint64_t time;

	/* Drop older entries */
	uint64_t min_update_index;
};

/* compacts all reftables into a giant table. Expire reflog entries if config is
 * non-NULL */
int stack_compact_all(struct stack *st, struct log_expiry_config *config);

/* heuristically compact unbalanced table stack. */
int stack_auto_compact(struct stack *st);

/* convenience function to read a single ref. Returns < 0 for error, 0
   for success, and 1 if ref not found. */
int stack_read_ref(struct stack *st, const char *refname,
		   struct ref_record *ref);

/* convenience function to read a single log. Returns < 0 for error, 0
   for success, and 1 if ref not found. */
int stack_read_log(struct stack *st, const char *refname,
		   struct log_record *log);

/* statistics on past compactions. */
struct compaction_stats {
	uint64_t bytes;
	int attempts;
	int failures;
};

struct compaction_stats *stack_compaction_stats(struct stack *st);

#endif
