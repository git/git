#ifndef MERGE_ORT_H
#define MERGE_ORT_H

#include "merge-recursive.h"

struct commit;
struct tree;

struct merge_result {
	/* Whether the merge is clean */
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
};

/*
 * rename-detecting three-way merge with recursive ancestor consolidation.
 * working tree and index are untouched.
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
