/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_WRITER_H
#define REFTABLE_WRITER_H

#include "reftable-record.h"

#include <stdint.h>
#include <unistd.h> /* ssize_t */

/* Writing single reftables */

/* reftable_write_options sets options for writing a single reftable. */
struct reftable_write_options {
	/* boolean: do not pad out blocks to block size. */
	unsigned unpadded : 1;

	/* the blocksize. Should be less than 2^24. */
	uint32_t block_size;

	/* boolean: do not generate a SHA1 => ref index. */
	unsigned skip_index_objects : 1;

	/* how often to write complete keys in each block. */
	int restart_interval;

	/* 4-byte identifier ("sha1", "s256") of the hash.
	 * Defaults to SHA1 if unset
	 */
	uint32_t hash_id;

	/* boolean: do not check ref names for validity or dir/file conflicts.
	 */
	unsigned skip_name_check : 1;

	/* boolean: copy log messages exactly. If unset, check that the message
	 *   is a single line, and add '\n' if missing.
	 */
	unsigned exact_log_message : 1;
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
reftable_new_writer(ssize_t (*writer_func)(void *, const void *, size_t),
		    void *writer_arg, struct reftable_write_options *opts);

/* Set the range of update indices for the records we will add. When writing a
   table into a stack, the min should be at least
   reftable_stack_next_update_index(), or REFTABLE_API_ERROR is returned.

   For transactional updates to a stack, typically min==max, and the
   update_index can be obtained by inspeciting the stack. When converting an
   existing ref database into a single reftable, this would be a range of
   update-index timestamps.
 */
void reftable_writer_set_limits(struct reftable_writer *w, uint64_t min,
				uint64_t max);

/*
  Add a reftable_ref_record. The record should have names that come after
  already added records.

  The update_index must be within the limits set by
  reftable_writer_set_limits(), or REFTABLE_API_ERROR is returned. It is an
  REFTABLE_API_ERROR error to write a ref record after a log record.
*/
int reftable_writer_add_ref(struct reftable_writer *w,
			    struct reftable_ref_record *ref);

/*
  Convenience function to add multiple reftable_ref_records; the function sorts
  the records before adding them, reordering the records array passed in.
*/
int reftable_writer_add_refs(struct reftable_writer *w,
			     struct reftable_ref_record *refs, int n);

/*
  adds reftable_log_records. Log records are keyed by (refname, decreasing
  update_index). The key for the record added must come after the already added
  log records.
*/
int reftable_writer_add_log(struct reftable_writer *w,
			    struct reftable_log_record *log);

/*
  Convenience function to add multiple reftable_log_records; the function sorts
  the records before adding them, reordering records array passed in.
*/
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

#endif
