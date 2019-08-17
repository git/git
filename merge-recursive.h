#ifndef MERGE_RECURSIVE_H
#define MERGE_RECURSIVE_H

#include "string-list.h"
#include "unpack-trees.h"

struct commit;

struct repository;

struct merge_options {
	const char *ancestor;
	const char *branch1;
	const char *branch2;
	enum {
		MERGE_RECURSIVE_NORMAL = 0,
		MERGE_RECURSIVE_OURS,
		MERGE_RECURSIVE_THEIRS
	} recursive_variant;
	const char *subtree_shift;
	unsigned buffer_output; /* 1: output at end, 2: keep buffered */
	unsigned renormalize : 1;
	long xdl_opts;
	int verbosity;
	enum {
		MERGE_DIRECTORY_RENAMES_NONE = 0,
		MERGE_DIRECTORY_RENAMES_CONFLICT = 1,
		MERGE_DIRECTORY_RENAMES_TRUE = 2
	} detect_directory_renames;
	int diff_detect_rename;
	int merge_detect_rename;
	int diff_rename_limit;
	int merge_rename_limit;
	int rename_score;
	int needed_rename_limit;
	int show_rename_progress;
	int call_depth;
	struct strbuf obuf;
	struct hashmap current_file_dir_set;
	struct string_list df_conflict_file_set;
	struct unpack_trees_options unpack_opts;
	struct index_state orig_index;
	struct repository *repo;
};

void init_merge_options(struct merge_options *opt, struct repository *repo);

/* parse the option in s and update the relevant field of opt */
int parse_merge_opt(struct merge_options *opt, const char *s);

/*
 * RETURN VALUES: All the merge_* functions below return a value as follows:
 *   > 0     Merge was clean
 *   = 0     Merge had conflicts
 *   < 0     Merge hit an unexpected and unrecoverable problem (e.g. disk
 *             full) and aborted merge part-way through.
 */

/*
 * rename-detecting three-way merge, no recursion.
 *
 * Outputs:
 *   - See RETURN VALUES above
 *   - No commit is created
 *   - opt->repo->index has the new index
 *   - $GIT_INDEX_FILE is not updated
 *   - The working tree is updated with results of the merge
 */
int merge_trees(struct merge_options *opt,
		struct tree *head,
		struct tree *merge,
		struct tree *merge_base);

/*
 * merge_recursive is like merge_trees() but with recursive ancestor
 * consolidation and, if the commit is clean, creation of a commit.
 *
 * NOTE: empirically, about a decade ago it was determined that with more
 *       than two merge bases, optimal behavior was found when the
 *       merge_bases were passed in the order of oldest commit to newest
 *       commit.  Also, merge_bases will be consumed (emptied) so make a
 *       copy if you need it.
 *
 * Outputs:
 *   - See RETURN VALUES above
 *   - If merge is clean, a commit is created and its address written to *result
 *   - opt->repo->index has the new index
 *   - $GIT_INDEX_FILE is not updated
 *   - The working tree is updated with results of the merge
 */
int merge_recursive(struct merge_options *opt,
		    struct commit *h1,
		    struct commit *h2,
		    struct commit_list *merge_bases,
		    struct commit **result);

/*
 * merge_recursive_generic can operate on trees instead of commits, by
 * wrapping the trees into virtual commits, and calling merge_recursive().
 * It also writes out the in-memory index to disk if the merge is successful.
 *
 * Outputs:
 *   - See RETURN VALUES above
 *   - If merge is clean, a commit is created and its address written to *result
 *   - opt->repo->index has the new index
 *   - $GIT_INDEX_FILE is updated
 *   - The working tree is updated with results of the merge
 */
int merge_recursive_generic(struct merge_options *opt,
			    const struct object_id *head,
			    const struct object_id *merge,
			    int num_merge_bases,
			    const struct object_id **merge_bases,
			    struct commit **result);

#endif
