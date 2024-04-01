/*
 * Low level 3-way in-core file merge.
 */

#ifndef LL_MERGE_H
#define LL_MERGE_H

#include "xdiff/xdiff.h"

/**
 *
 * Calling sequence:
 * ----------------
 *
 * - Prepare a `struct ll_merge_options` to record options.
 *   If you have no special requests, skip this and pass `NULL`
 *   as the `opts` parameter to use the default options.
 *
 * - Allocate an mmbuffer_t variable for the result.
 *
 * - Allocate and fill variables with the file's original content
 *   and two modified versions (using `read_mmfile`, for example).
 *
 * - Call `ll_merge()`.
 *
 * - Read the merged content from `result_buf.ptr` and `result_buf.size`.
 *
 * - Release buffers when finished.  A simple
 *   `free(ancestor.ptr); free(ours.ptr); free(theirs.ptr);
 *   free(result_buf.ptr);` will do.
 *
 * If the modifications do not merge cleanly, `ll_merge` will return a
 * nonzero value and `result_buf` will generally include a description of
 * the conflict bracketed by markers such as the traditional `<<<<<<<`
 * and `>>>>>>>`.
 *
 * The `ancestor_label`, `our_label`, and `their_label` parameters are
 * used to label the different sides of a conflict if the merge driver
 * supports this.
 */


struct index_state;

/**
 * This describes the set of options the calling program wants to affect
 * the operation of a low-level (single file) merge.
 */
struct ll_merge_options {

	/**
	 * Behave as though this were part of a merge between common ancestors in
	 * a recursive merge (merges of binary files may need to be handled
	 * differently in such cases, for example). If a helper program is
	 * specified by the `[merge "<driver>"] recursive` configuration, it will
	 * be used.
	 */
	unsigned virtual_ancestor : 1;

	/**
	 * Resolve local conflicts automatically in favor of one side or the other
	 * (as in 'git merge-file' `--ours`/`--theirs`/`--union`).  Can be `0`,
	 * `XDL_MERGE_FAVOR_OURS`, `XDL_MERGE_FAVOR_THEIRS`,
	 * or `XDL_MERGE_FAVOR_UNION`.
	 */
	unsigned variant : 2;

	/**
	 * Resmudge and clean the "base", "theirs" and "ours" files before merging.
	 * Use this when the merge is likely to have overlapped with a change in
	 * smudge/clean or end-of-line normalization rules.
	 */
	unsigned renormalize : 1;

	/**
	 * Increase the length of conflict markers so that nested conflicts
	 * can be differentiated.
	 */
	unsigned extra_marker_size;

	/* Override the global conflict style. */
	int conflict_style;

	/* Extra xpparam_t flags as defined in xdiff/xdiff.h. */
	long xdl_opts;
};

#define LL_MERGE_OPTIONS_INIT { .conflict_style = -1 }

enum ll_merge_result {
	LL_MERGE_ERROR = -1,
	LL_MERGE_OK = 0,
	LL_MERGE_CONFLICT,
	LL_MERGE_BINARY_CONFLICT,
};

/**
 * Perform a three-way single-file merge in core.  This is a thin wrapper
 * around `xdl_merge` that takes the path and any merge backend specified in
 * `.gitattributes` or `.git/info/attributes` into account.
 * Returns 0 for a clean merge.
 */
enum ll_merge_result ll_merge(mmbuffer_t *result_buf,
	     const char *path,
	     mmfile_t *ancestor, const char *ancestor_label,
	     mmfile_t *ours, const char *our_label,
	     mmfile_t *theirs, const char *their_label,
	     struct index_state *istate,
	     const struct ll_merge_options *opts);

int ll_merge_marker_size(struct index_state *istate, const char *path);
void reset_merge_attributes(void);

#endif
