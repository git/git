#ifndef MERGE_ORT_H
#define MERGE_ORT_H

#include "hash.h"
#include "strbuf.h"

struct commit;
struct commit_list;
struct tree;
struct strmap;

struct merge_result {
	/*
	 * Whether the merge is clean; possible values:
	 *    1: clean
	 *    0: not clean (merge conflicts)
	 *   <0: operation aborted prematurely.  (object database
	 *       unreadable, disk full, etc.)  Worktree may be left in an
	 *       inconsistent state if operation failed near the end.
	 */
	int clean;

	/*
	 * Result of merge.  If !clean, represents what would go in worktree
	 * (thus possibly including files containing conflict markers).
	 */
	struct tree *tree;

	/*
	 * Special messages and conflict notices for various paths
	 *
	 * This is a map of pathnames to a string_list. It contains various
	 * warning/conflict/notice messages (possibly multiple per path)
	 * that callers may want to use.
	 */
	struct strmap *path_messages;

	/*
	 * Additional metadata used by merge_switch_to_result() or future calls
	 * to merge_incore_*().  Includes data needed to update the index (if
	 * !clean) and to print "CONFLICT" messages.  Not for external use.
	 */
	void *priv;
	/* Also private */
	unsigned _properly_initialized;
};

struct merge_options_internal;
struct merge_options {
	struct repository *repo;

	/* ref names used in console messages and conflict markers */
	const char *ancestor;
	const char *branch1;
	const char *branch2;

	/* rename related options */
	int detect_renames;
	enum {
		MERGE_DIRECTORY_RENAMES_NONE = 0,
		MERGE_DIRECTORY_RENAMES_CONFLICT = 1,
		MERGE_DIRECTORY_RENAMES_TRUE = 2
	} detect_directory_renames;
	int rename_limit;
	int rename_score;
	int show_rename_progress;

	/* xdiff-related options (patience, ignore whitespace, ours/theirs) */
	long xdl_opts;
	int conflict_style;
	enum {
		MERGE_VARIANT_NORMAL = 0,
		MERGE_VARIANT_OURS,
		MERGE_VARIANT_THEIRS
	} recursive_variant;

	/* console output related options */
	int verbosity;
	unsigned buffer_output; /* 1: output at end, 2: keep buffered */
	struct strbuf obuf;     /* output buffer; if buffer_output == 2, caller
				 * must handle and call strbuf_release */

	/* miscellaneous control options */
	const char *subtree_shift;
	unsigned renormalize : 1;
	unsigned mergeability_only : 1; /* exit early, write fewer objects */
	unsigned record_conflict_msgs_as_headers : 1;
	const char *msg_header_prefix;

	/* internal fields used by the implementation */
	struct merge_options_internal *priv;
};

/* Mostly internal function also used by merge-ort-wrappers.c */
struct commit *make_virtual_commit(struct repository *repo,
				   struct tree *tree,
				   const char *comment);

/*
 * rename-detecting three-way merge with recursive ancestor consolidation.
 * working tree and index are untouched.
 *
 * merge_bases will be consumed (emptied) so make a copy if you need it.
 *
 * NOTE: empirically, the recursive algorithm will perform better if you
 *       pass the merge_bases in the order of oldest commit to the
 *       newest[1][2].
 *
 *       [1] https://lore.kernel.org/git/nycvar.QRO.7.76.6.1907252055500.21907@tvgsbejvaqbjf.bet/
 *       [2] commit 8918b0c9c2 ("merge-recur: try to merge older merge bases
 *           first", 2006-08-09)
 */
void merge_incore_recursive(struct merge_options *opt,
			    const struct commit_list *merge_bases,
			    struct commit *side1,
			    struct commit *side2,
			    struct merge_result *result);

/*
 * rename-detecting three-way merge, no recursion.
 * working tree and index are untouched.
 */
void merge_incore_nonrecursive(struct merge_options *opt,
			       struct tree *merge_base,
			       struct tree *side1,
			       struct tree *side2,
			       struct merge_result *result);

/* Update the working tree and index from head to result after incore merge */
void merge_switch_to_result(struct merge_options *opt,
			    struct tree *head,
			    struct merge_result *result,
			    int update_worktree_and_index,
			    int display_update_msgs);

/*
 * Display messages about conflicts and which files were 3-way merged.
 * Automatically called by merge_switch_to_result() with stream == stdout,
 * so only call this when bypassing merge_switch_to_result().
 */
void merge_display_update_messages(struct merge_options *opt,
				   int detailed,
				   struct merge_result *result);

struct stage_info {
	struct object_id oid;
	int mode;
	int stage;
};

/*
 * Provide a list of path -> {struct stage_info*} mappings for
 * all conflicted files.  Note that each path could appear up to three
 * times in the list, corresponding to 3 different stage entries.  In short,
 * this basically provides the info that would be printed by `ls-files -u`.
 *
 * result should have been populated by a call to
 * one of the merge_incore_[non]recursive() functions.
 *
 * conflicted_files should be empty before calling this function.
 */
void merge_get_conflicted_files(struct merge_result *result,
				struct string_list *conflicted_files);

/* Do needed cleanup when not calling merge_switch_to_result() */
void merge_finalize(struct merge_options *opt,
		    struct merge_result *result);


/* for use by porcelain commands */
void init_ui_merge_options(struct merge_options *opt, struct repository *repo);
/* for use by plumbing commands */
void init_basic_merge_options(struct merge_options *opt, struct repository *repo);

void copy_merge_options(struct merge_options *dst, struct merge_options *src);
void clear_merge_options(struct merge_options *opt);

/* parse the option in s and update the relevant field of opt */
int parse_merge_opt(struct merge_options *opt, const char *s);

#endif
