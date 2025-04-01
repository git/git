#ifndef MERGE_ORT_WRAPPERS_H
#define MERGE_ORT_WRAPPERS_H

#include "merge-ort.h"

/*
 * rename-detecting three-way merge, no recursion.
 * Wrapper mimicking the old merge_trees() function.
 */
int merge_ort_nonrecursive(struct merge_options *opt,
			   struct tree *head,
			   struct tree *merge,
			   struct tree *common);

/*
 * rename-detecting three-way merge with recursive ancestor consolidation.
 * Wrapper mimicking the old merge_recursive() function.
 */
int merge_ort_recursive(struct merge_options *opt,
			struct commit *h1,
			struct commit *h2,
			const struct commit_list *ancestors,
			struct commit **result);

/*
 * rename-detecting three-way merge.  num_merge_bases must be at least 1.
 * Recursive ancestor consolidation will be performed if num_merge_bases > 1.
 * Wrapper mimicking the old merge_recursive_generic() function.
 */
int merge_ort_generic(struct merge_options *opt,
		      const struct object_id *head,
		      const struct object_id *merge,
		      int num_merge_bases,
		      const struct object_id *merge_bases,
		      struct commit **result);

#endif
