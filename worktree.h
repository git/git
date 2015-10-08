#ifndef WORKTREE_H
#define WORKTREE_H

struct worktree {
	char *path;
	char *git_dir;
	char *head_ref;
	unsigned char head_sha1[20];
	int is_detached;
	int is_bare;
};

/* Functions for acting on the information about worktrees. */

/*
 * Get the worktrees.  The primary worktree will always be the first returned,
 * and linked worktrees will be pointed to by 'next' in each subsequent
 * worktree.  No specific ordering is done on the linked worktrees.
 *
 * The caller is responsible for freeing the memory from the returned
 * worktree(s).
 */
extern struct worktree **get_worktrees(void);

/*
 * Free up the memory for worktree(s)
 */
extern void free_worktrees(struct worktree **);

/*
 * Check if a per-worktree symref points to a ref in the main worktree
 * or any linked worktree, and return the path to the exising worktree
 * if it is.  Returns NULL if there is no existing ref.  The caller is
 * responsible for freeing the returned path.
 */
extern char *find_shared_symref(const char *symref, const char *target);

#endif
