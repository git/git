#ifndef MERGE_ORT_H
#define MERGE_ORT_H

#include "merge-recursive.h"

struct commit;
struct tree;

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
	 * Additional metadata used by merge_switch_to_result() or future calls
	 * to merge_incore_*().  Includes data needed to update the index (if
	 * !clean) and to print "CONFLICT" messages.  Not for external use.
	 */
	void *priv;
	/* Also private */
	unsigned _properly_initialized;
};

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
			    struct commit_list *merge_bases,
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

/* Do needed cleanup when not calling merge_switch_to_result() */
void merge_finalize(struct merge_options *opt,
		    struct merge_result *result);

#endif
