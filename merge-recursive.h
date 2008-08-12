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
extern int merge_recursive_generic(const char **base_list,
		const unsigned char *head_sha1, const char *head_name,
		const unsigned char *next_sha1, const char *next_name);
int merge_recursive_config(const char *var, const char *value, void *cb);
void merge_recursive_setup(int is_subtree_merge);
struct tree *write_tree_from_memory(void);

extern int merge_recursive_verbosity;

#endif
