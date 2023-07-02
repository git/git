#ifndef MERGE_RECURSIVE_H
#define MERGE_RECURSIVE_H

#include "strbuf.h"

struct commit;
struct commit_list;
struct object_id;
struct repository;
struct tree;

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
	unsigned record_conflict_msgs_as_headers : 1;
	const char *msg_header_prefix;

	/* internal fields used by the implementation */
	struct merge_options_internal *priv;
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
 *   - opt->repo->index has the new index
 *   - new index NOT written to disk
 *   - The working tree is updated with results of the merge
 */
int merge_trees(struct merge_options *opt,
		struct tree *head,
		struct tree *merge,
		struct tree *merge_base);

/*
 * merge_recursive is like merge_trees() but with recursive ancestor
 * consolidation.
 *
 * NOTE: empirically, about a decade ago it was determined that with more
 *       than two merge bases, optimal behavior was found when the
 *       merge_bases were passed in the order of oldest commit to newest
 *       commit.  Also, merge_bases will be consumed (emptied) so make a
 *       copy if you need it.
 *
 * Outputs:
 *   - See RETURN VALUES above
 *   - *result is treated as scratch space for temporary recursive merges
 *   - opt->repo->index has the new index
 *   - new index NOT written to disk
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
 *   - *result is treated as scratch space for temporary recursive merges
 *   - opt->repo->index has the new index
 *   - new index also written to $GIT_INDEX_FILE on disk
 *   - The working tree is updated with results of the merge
 */
int merge_recursive_generic(struct merge_options *opt,
			    const struct object_id *head,
			    const struct object_id *merge,
			    int num_merge_bases,
			    const struct object_id **merge_bases,
			    struct commit **result);

#endif
