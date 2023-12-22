#ifndef REFS_PACKED_BACKEND_H
#define REFS_PACKED_BACKEND_H

#include "../cache.h"
#include "refs-internal.h"
#include "../lockfile.h"

struct repository;
struct ref_transaction;

/*
 * Support for storing references in a `packed-refs` file.
 *
 * Note that this backend doesn't check for D/F conflicts, because it
 * doesn't care about them. But usually it should be wrapped in a
 * `files_ref_store` that prevents D/F conflicts from being created,
 * even among packed refs.
 */

struct ref_store *packed_ref_store_create(struct repository *repo,
					  const char *gitdir,
					  unsigned int store_flags);

/*
 * Lock the packed-refs file for writing. Flags is passed to
 * hold_lock_file_for_update(). Return 0 on success. On errors, write
 * an error message to `err` and return a nonzero value.
 */
int packed_refs_lock(struct ref_store *ref_store, int flags, struct strbuf *err);

void packed_refs_unlock(struct ref_store *ref_store);
int packed_refs_is_locked(struct ref_store *ref_store);

/*
 * Return true if `transaction` really needs to be carried out against
 * the specified packed_ref_store, or false if it can be skipped
 * (i.e., because it is an obvious NOOP). `ref_store` must be locked
 * before calling this function.
 */
int is_packed_transaction_needed(struct ref_store *ref_store,
				 struct ref_transaction *transaction);

struct packed_ref_store;

/*
 * A `snapshot` represents one snapshot of a `packed-refs` file.
 *
 * Normally, this will be a mmapped view of the contents of the
 * `packed-refs` file at the time the snapshot was created. However,
 * if the `packed-refs` file was not sorted, this might point at heap
 * memory holding the contents of the `packed-refs` file with its
 * records sorted by refname.
 *
 * `snapshot` instances are reference counted (via
 * `acquire_snapshot()` and `release_snapshot()`). This is to prevent
 * an instance from disappearing while an iterator is still iterating
 * over it. Instances are garbage collected when their `referrers`
 * count goes to zero.
 *
 * The most recent `snapshot`, if available, is referenced by the
 * `packed_ref_store`. Its freshness is checked whenever
 * `get_snapshot()` is called; if the existing snapshot is obsolete, a
 * new snapshot is taken.
 */
struct snapshot {
	/*
	 * A back-pointer to the packed_ref_store with which this
	 * snapshot is associated:
	 */
	struct packed_ref_store *refs;

	/* Is the `packed-refs` file currently mmapped? */
	int mmapped;

	/* which file format version is this file? */
	int version;

	/*
	 * The contents of the `packed-refs` file:
	 *
	 * - buf -- a pointer to the start of the memory
	 * - start -- a pointer to the first byte of actual references
	 *   (i.e., after the header line, if one is present)
	 * - eof -- a pointer just past the end of the reference
	 *   contents
	 *
	 * If the `packed-refs` file was already sorted, `buf` points
	 * at the mmapped contents of the file. If not, it points at
	 * heap-allocated memory containing the contents, sorted. If
	 * there were no contents (e.g., because the file didn't
	 * exist), `buf`, `start`, and `eof` are all NULL.
	 */
	char *buf, *start, *eof;

	/*
	 * What is the peeled state of the `packed-refs` file that
	 * this snapshot represents? (This is usually determined from
	 * the file's header.)
	 */
	enum { PEELED_NONE, PEELED_TAGS, PEELED_FULLY } peeled;

	/*************************
	 * packed-refs v2 values *
	 *************************/
	size_t nr;
	size_t prefixes_nr;
	size_t buflen;
	const unsigned char *offset_chunk;
	const char *refs_chunk;
	const unsigned char *prefix_offsets_chunk;
	const char *prefix_chunk;

	/*
	 * Count of references to this instance, including the pointer
	 * from `packed_ref_store::snapshot`, if any. The instance
	 * will not be freed as long as the reference count is
	 * nonzero.
	 */
	unsigned int referrers;

	/*
	 * The metadata of the `packed-refs` file from which this
	 * snapshot was created, used to tell if the file has been
	 * replaced since we read it.
	 */
	struct stat_validity validity;
};

int release_snapshot(struct snapshot *snapshot);

/*
 * If the buffer in `snapshot` is active, then either munmap the
 * memory and close the file, or free the memory. Then set the buffer
 * pointers to NULL.
 */
void clear_snapshot_buffer(struct snapshot *snapshot);

/*
 * A `ref_store` representing references stored in a `packed-refs`
 * file. It implements the `ref_store` interface, though it has some
 * limitations:
 *
 * - It cannot store symbolic references.
 *
 * - It cannot store reflogs.
 *
 * - It does not support reference renaming (though it could).
 *
 * On the other hand, it can be locked outside of a reference
 * transaction. In that case, it remains locked even after the
 * transaction is done and the new `packed-refs` file is activated.
 */
struct packed_ref_store {
	struct ref_store base;

	unsigned int store_flags;

	/* The path of the "packed-refs" file: */
	char *path;

	/*
	 * A snapshot of the values read from the `packed-refs` file,
	 * if it might still be current; otherwise, NULL.
	 */
	struct snapshot *snapshot;

	/*
	 * Lock used for the "packed-refs" file. Note that this (and
	 * thus the enclosing `packed_ref_store`) must not be freed.
	 */
	struct lock_file lock;

	/*
	 * Temporary file used when rewriting new contents to the
	 * "packed-refs" file. Note that this (and thus the enclosing
	 * `packed_ref_store`) must not be freed.
	 */
	struct tempfile *tempfile;
};

/*
 * This value is set in `base.flags` if the peeled value of the
 * current reference is known. In that case, `peeled` contains the
 * correct peeled value for the reference, which might be `null_oid`
 * if the reference is not a tag or if it is broken.
 */
#define REF_KNOWS_PEELED 0x40

/*
 * An iterator over a snapshot of a `packed-refs` file.
 */
struct packed_ref_iterator {
	struct ref_iterator base;
	struct snapshot *snapshot;
	struct repository *repo;
	unsigned int flags;
	int version;

	/* Scratch space for current values: */
	struct object_id oid, peeled;
	struct strbuf refname_buf;

	/* The current position in the snapshot's buffer: */
	const char *pos;

	/***********************************
	 * packed-refs v1 iterator values. *
	 ***********************************/

	/* The end of the part of the buffer that will be iterated over: */
	const char *eof;

	/***********************************
	 * packed-refs v2 iterator values. *
	 ***********************************/
	size_t nr;
	size_t row;
	size_t prefix_row_end;
	size_t prefix_i;
	const char *cur_prefix;
};

typedef int (*write_ref_fn)(const char *refname,
			    const struct object_id *oid,
			    const struct object_id *peeled,
			    void *write_data);

int merge_iterator_and_updates(struct packed_ref_store *refs,
			       struct string_list *updates,
			       struct strbuf *err,
			       write_ref_fn write_fn,
			       void *write_data);

/**
 * Parse the buffer at the given snapshot to verify that it is a
 * packed-refs file in version 1 format. Update the snapshot->peeled
 * value according to the header information. Update the given
 * 'sorted' value with whether or not the packed-refs file is sorted.
 */
int parse_packed_format_v1_header(struct packed_ref_store *refs,
				  struct snapshot *snapshot,
				  int *sorted);

/*
 * Find the place in `snapshot->buf` where the start of the record for
 * `refname` starts. If `mustexist` is true and the reference doesn't
 * exist, then return NULL. If `mustexist` is false and the reference
 * doesn't exist, then return the point where that reference would be
 * inserted, or `snapshot->eof` (which might be NULL) if it would be
 * inserted at the end of the file. In the latter mode, `refname`
 * doesn't have to be a proper reference name; for example, one could
 * search for "refs/replace/" to find the start of any replace
 * references.
 *
 * The record is sought using a binary search, so `snapshot->buf` must
 * be sorted.
 */
const char *find_reference_location_v1(struct snapshot *snapshot,
				       const char *refname, int mustexist);

int packed_read_raw_ref_v1(struct packed_ref_store *refs, struct snapshot *snapshot,
			   const char *refname, struct object_id *oid,
			   unsigned int *type, int *failure_errno);

void verify_buffer_safe_v1(struct snapshot *snapshot);
void sort_snapshot_v1(struct snapshot *snapshot);
int write_packed_file_header_v1(FILE *out);
int next_record_v1(struct packed_ref_iterator *iter);
int write_packed_entry_v1(const char *refname,
			  const struct object_id *oid,
			  const struct object_id *peeled,
			  void *write_data);

/**
 * Parse the buffer at the given snapshot to verify that it is a
 * packed-refs file in version 1 format. Update the snapshot->peeled
 * value according to the header information. Update the given
 * 'sorted' value with whether or not the packed-refs file is sorted.
 */
int parse_packed_format_v1_header(struct packed_ref_store *refs,
				  struct snapshot *snapshot,
				  int *sorted);

int detect_packed_format_v2_header(struct packed_ref_store *refs,
				   struct snapshot *snapshot);
/*
 * Find the place in `snapshot->buf` where the start of the record for
 * `refname` starts. If `mustexist` is true and the reference doesn't
 * exist, then return NULL. If `mustexist` is false and the reference
 * doesn't exist, then return the point where that reference would be
 * inserted, or `snapshot->eof` (which might be NULL) if it would be
 * inserted at the end of the file. In the latter mode, `refname`
 * doesn't have to be a proper reference name; for example, one could
 * search for "refs/replace/" to find the start of any replace
 * references.
 *
 * The record is sought using a binary search, so `snapshot->buf` must
 * be sorted.
 */
const char *find_reference_location_v2(struct snapshot *snapshot,
				       const char *refname, int mustexist,
				       size_t *pos);

int packed_read_raw_ref_v2(struct packed_ref_store *refs, struct snapshot *snapshot,
			   const char *refname, struct object_id *oid,
			   unsigned int *type, int *failure_errno);
int next_record_v2(struct packed_ref_iterator *iter);
void fill_snapshot_v2(struct snapshot *snapshot);

struct write_packed_refs_v2_context;
struct write_packed_refs_v2_context *create_v2_context(struct packed_ref_store *refs,
						       struct string_list *updates,
						       struct strbuf *err);
int write_packed_refs_v2(struct write_packed_refs_v2_context *ctx);
void free_v2_context(struct write_packed_refs_v2_context *ctx);

void init_iterator_prefix_info(const char *prefix,
			       struct packed_ref_iterator *iter);

#endif /* REFS_PACKED_BACKEND_H */
