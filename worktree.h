#ifndef WORKTREE_H
#define WORKTREE_H

struct worktree {
	char *path;
	char *id;
	char *head_ref;		/* NULL if HEAD is broken or detached */
	char *lock_reason;	/* internal use */
	unsigned char head_sha1[20];
	int is_detached;
	int is_bare;
	int is_current;
	int lock_reason_valid;
};

/* Functions for acting on the information about worktrees. */

#define GWT_SORT_LINKED (1 << 0) /* keeps linked worktrees sorted */

/*
 * Get the worktrees.  The primary worktree will always be the first returned,
 * and linked worktrees will be pointed to by 'next' in each subsequent
 * worktree.  No specific ordering is done on the linked worktrees.
 *
 * The caller is responsible for freeing the memory from the returned
 * worktree(s).
 */
extern struct worktree **get_worktrees(unsigned flags);

/*
 * Returns 1 if linked worktrees exist, 0 otherwise.
 */
extern int submodule_uses_worktrees(const char *path);

/*
 * Return git dir of the worktree. Note that the path may be relative.
 * If wt is NULL, git dir of current worktree is returned.
 */
extern const char *get_worktree_git_dir(const struct worktree *wt);

/*
 * Search a worktree that can be unambiguously identified by
 * "arg". "prefix" must not be NULL.
 */
extern struct worktree *find_worktree(struct worktree **list,
				      const char *prefix,
				      const char *arg);

/*
 * Return true if the given worktree is the main one.
 */
extern int is_main_worktree(const struct worktree *wt);

/*
 * Return the reason string if the given worktree is locked or NULL
 * otherwise.
 */
extern const char *is_worktree_locked(struct worktree *wt);

/*
 * Free up the memory for worktree(s)
 */
extern void free_worktrees(struct worktree **);

/*
 * Check if a per-worktree symref points to a ref in the main worktree
 * or any linked worktree, and return the worktree that holds the ref,
 * or NULL otherwise. The result may be destroyed by the next call.
 */
extern const struct worktree *find_shared_symref(const char *symref,
						 const char *target);

int is_worktree_being_rebased(const struct worktree *wt, const char *target);
int is_worktree_being_bisected(const struct worktree *wt, const char *target);

/*
 * Similar to git_path() but can produce paths for a specified
 * worktree instead of current one
 */
extern const char *worktree_git_path(const struct worktree *wt,
				     const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

#endif
