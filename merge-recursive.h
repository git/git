#ifndef MERGE_RECURSIVE_H
#define MERGE_RECURSIVE_H

int merge_recursive(struct commit *h1,
		    struct commit *h2,
		    const char *branch1,
		    const char *branch2,
		    struct commit_list *ancestors,
		    struct commit **result);

int merge_trees(struct tree *head,
		struct tree *merge,
		struct tree *common,
		const char *branch1,
		const char *branch2,
		struct tree **result);

struct tree *write_tree_from_memory(void);

#endif
