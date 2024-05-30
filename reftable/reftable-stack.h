/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_STACK_H
#define REFTABLE_STACK_H

#include "reftable-writer.h"

/*
 * The stack presents an interface to a mutable sequence of reftables.

 * A stack can be mutated by pushing a table to the top of the stack.

 * The reftable_stack automatically compacts files on disk to ensure good
 * amortized performance.
 *
 * For windows and other platforms that cannot have open files as rename
 * destinations, concurrent access from multiple processes needs the rand()
 * random seed to be randomized.
 */
struct reftable_stack;

/* open a new reftable stack. The tables along with the table list will be
 *  stored in 'dir'. Typically, this should be .git/reftables.
 */
int reftable_new_stack(struct reftable_stack **dest, const char *dir,
		       const struct reftable_write_options *opts);

/* returns the update_index at which a next table should be written. */
uint64_t reftable_stack_next_update_index(struct reftable_stack *st);

/* holds a transaction to add tables at the top of a stack. */
struct reftable_addition;

/*
 * returns a new transaction to add reftables to the given stack. As a side
 * effect, the ref database is locked.
 */
int reftable_stack_new_addition(struct reftable_addition **dest,
				struct reftable_stack *st);

/* Adds a reftable to transaction. */
int reftable_addition_add(struct reftable_addition *add,
			  int (*write_table)(struct reftable_writer *wr,
					     void *arg),
			  void *arg);

/* Commits the transaction, releasing the lock. After calling this,
 * reftable_addition_destroy should still be called.
 */
int reftable_addition_commit(struct reftable_addition *add);

/* Release all non-committed data from the transaction, and deallocate the
 * transaction. Releases the lock if held. */
void reftable_addition_destroy(struct reftable_addition *add);

/* add a new table to the stack. The write_table function must call
 * reftable_writer_set_limits, add refs and return an error value. */
int reftable_stack_add(struct reftable_stack *st,
		       int (*write_table)(struct reftable_writer *wr,
					  void *write_arg),
		       void *write_arg);

struct reftable_iterator;

/*
 * Initialize an iterator for the merged tables contained in the stack that can
 * be used to iterate through refs. The iterator is valid until the next reload
 * or write.
 */
void reftable_stack_init_ref_iterator(struct reftable_stack *st,
				      struct reftable_iterator *it);

/*
 * Initialize an iterator for the merged tables contained in the stack that can
 * be used to iterate through logs. The iterator is valid until the next reload
 * or write.
 */
void reftable_stack_init_log_iterator(struct reftable_stack *st,
				      struct reftable_iterator *it);

/* returns the merged_table for seeking. This table is valid until the
 * next write or reload, and should not be closed or deleted.
 */
struct reftable_merged_table *
reftable_stack_merged_table(struct reftable_stack *st);

/* frees all resources associated with the stack. */
void reftable_stack_destroy(struct reftable_stack *st);

/* Reloads the stack if necessary. This is very cheap to run if the stack was up
 * to date */
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

/* delete stale .ref tables. */
int reftable_stack_clean(struct reftable_stack *st);

/* convenience function to read a single ref. Returns < 0 for error, 0 for
 * success, and 1 if ref not found. */
int reftable_stack_read_ref(struct reftable_stack *st, const char *refname,
			    struct reftable_ref_record *ref);

/* convenience function to read a single log. Returns < 0 for error, 0 for
 * success, and 1 if ref not found. */
int reftable_stack_read_log(struct reftable_stack *st, const char *refname,
			    struct reftable_log_record *log);

/* statistics on past compactions. */
struct reftable_compaction_stats {
	uint64_t bytes; /* total number of bytes written */
	uint64_t entries_written; /* total number of entries written, including
				     failures. */
	int attempts; /* how often we tried to compact */
	int failures; /* failures happen on concurrent updates */
};

/* return statistics for compaction up till now. */
struct reftable_compaction_stats *
reftable_stack_compaction_stats(struct reftable_stack *st);

/* print the entire stack represented by the directory */
int reftable_stack_print_directory(const char *stackdir, uint32_t hash_id);

#endif
