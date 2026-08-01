#ifndef DIFF_HUNKS_H
#define DIFF_HUNKS_H

#include "hash.h"
#include "xdiff-interface.h"	/* xdl_emit_hunk_consume_func_t */

struct object_id;
struct repository;
struct object_database;

/*
 * A persistent store of precomputed diff hunk coordinates, at
 * .git/objects/info/diff-hunks. Entries are keyed by the two blobs diffed
 * and the xdl_opts they were diffed under, so a cached result is valid
 * in any context that key recurs in, independent of path. The xdl_opts
 * key component mirrors the (always non-negative) diff_options field it
 * projects from, and is serialized and compared as a 4-byte big-endian
 * integer.
 *
 * The hunks a pair produces are not unique. They vary with the xdiff
 * algorithm and ignore flags (xdl_opts, part of the key), and with
 * whether the diff was trimmed: a zero-context diff runs
 * trim_common_tail, which can pick a different but equally valid set of
 * hunks than an untrimmed diff. The store holds one entry per key, so a
 * pair is recorded only when its trimmed and untrimmed diffs are
 * identical (the recording caller checks); such an entry serves a
 * consumer at any context. The rare pair where the two diffs differ is
 * never recorded and is always computed.
 *
 * The store is a cache: ordinary commands read it and fall back to
 * computing the diff when it is absent, stale, or corrupt. It is filled
 * as a side effect of diff and log runs, but only when writing is
 * enabled (such a write-enabled run is a warming run); writing is off
 * by default, so an ordinary command reads the store without recording
 * into it.
 */

/*
 * A hunk's coordinates. The type is long to match the xdiff emit
 * callback; the values are a diff's line numbers and counts, always
 * within the int32 range the on-disk format stores (see
 * diff_hunks_writer_add()).
 */
struct precomputed_hunk {
	long old_start;
	long old_count;
	long new_start;
	long new_count;
};

/*
 * The repository's store, loaded once on first use and cached on the
 * object database. Returns NULL when reading is disabled
 * (core.diffHunks=false), the store is absent, or it fails to parse
 * (wrong signature, version, or object hash, or a corrupt structure).
 * The lookup functions below accept a NULL store and treat it as
 * empty (every lookup misses), so callers need not check for NULL.
 * The object database owns the store; callers must not free it.
 */
struct diff_hunks_store *repo_diff_hunks_store(struct repository *r);

/* Free the repository's cached store, at object-database teardown. */
void close_diff_hunks_store(struct object_database *o);

/*
 * Consultation counters for the repository's store: pairs the store
 * served (hits) and pairs it was consulted for but could not serve
 * (misses). Both zero when reading is disabled or no store exists.
 */
void diff_hunks_read_stats(struct repository *r,
			   unsigned long *hits, unsigned long *misses);

/*
 * Replay the recorded hunks of an (old blob, new blob) pair diffed
 * under xdl_opts through hunk_func. The sequence is validated before
 * any callback runs: on a hit (return 1) every hunk is emitted, on a
 * miss (return 0: absent pair, xdl_opts mismatch, or an entry that
 * fails validation) nothing is emitted, so a caller may accumulate
 * directly into its result.
 */
int diff_hunks_replay(struct diff_hunks_store *s,
		      const struct object_id *old_oid,
		      const struct object_id *new_oid,
		      int xdl_opts,
		      xdl_emit_hunk_consume_func_t hunk_func, void *cb_data);

/*
 * A warming run's writer: it accumulates the hunks it computes in memory
 * and flushes them to the store in one pass at finish.
 */
struct diff_hunks_writer;

/*
 * Return a writer for a warming run, or NULL when writing is disabled
 * (the default). diff_hunks_writer_add() tolerates a NULL writer, so a
 * caller may attach the result unconditionally. Pair with
 * diff_hunks_writer_finish().
 */
struct diff_hunks_writer *diff_hunks_writer_maybe_new(struct repository *r);

/*
 * Record a blob pair's hunks as computed under xdl_opts; a later lookup
 * with a matching key is served these hunks. The caller must have
 * checked that the pair's trimmed and untrimmed diffs are identical
 * (see the top of this file), so the entry answers at any context;
 * diff_hunks_writer_record_stable() below performs that check.
 * NULL-safe. Returns 1 when the entry was recorded, 0 when the writer
 * refused it (no hunks, a null object id, or values the on-disk
 * 32-bit fields cannot hold).
 */
int diff_hunks_writer_add(struct diff_hunks_writer *w,
			  const struct object_id *old_oid,
			  const struct object_id *new_oid,
			  int xdl_opts,
			  const struct precomputed_hunk *hunks,
			  size_t nr_hunks);

/*
 * Record the pair only if it is trim-stable: the recording caller
 * hands over both the trimmed (xdi_diff) and untrimmed (xdl_diff)
 * zero-context hunk sequences it computed, and the entry is added
 * only when the two are identical. NULL-safe.
 */
void diff_hunks_writer_record_stable(struct diff_hunks_writer *w,
				     const struct object_id *old_oid,
				     const struct object_id *new_oid,
				     int xdl_opts,
				     const struct precomputed_hunk *trimmed,
				     size_t nr_trimmed,
				     const struct precomputed_hunk *full,
				     size_t nr_full);

/* Flush the accumulated entries to the store and free the writer. NULL-safe. */
void diff_hunks_writer_finish(struct diff_hunks_writer *w);

/* Remove the store file. Returns 0 (incl. absent) or -1. */
int diff_hunks_clear(struct repository *r);
/* Validate the store. Returns 0 if valid/absent, -1 if corrupt. */
int diff_hunks_verify(struct repository *r);

#endif /* DIFF_HUNKS_H */
