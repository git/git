#ifndef TREE_VERIFY_H
#define TREE_VERIFY_H

struct commit_list;
struct repository;

/*
 * Verify trees of commits incrementally against their parents.
 * Returns 0 on success, or reports an error and returns -1.
 */
int verify_commits_incremental(struct repository *repo,
			       struct commit_list **commits,
			       int exclude_promisor_objects);

#endif /* TREE_VERIFY_H */
