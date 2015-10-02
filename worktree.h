#ifndef WORKTREE_H
#define WORKTREE_H

/*
 * Check if a per-worktree symref points to a ref in the main worktree
 * or any linked worktree, and return the path to the exising worktree
 * if it is.  Returns NULL if there is no existing ref.  The caller is
 * responsible for freeing the returned path.
 */
extern char *find_shared_symref(const char *symref, const char *target);

#endif
