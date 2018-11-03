#ifndef WORKTREE_H
#define WORKTREE_H

#include "cache.h"
#include "refs.h"

struct strbuf;

struct worktree {
	char *path;
	char *id;
	char *head_ref;		/* NULL if HEAD is broken or detached */
	char *lock_reason;	/* private - use worktree_lock_reason */
	struct object_id head_oid;
	int is_detached;
	int is_bare;
	int is_current;
	int lock_reason_valid; /* private */
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
extern const char *worktree_lock_reason(struct worktree *wt);

#define WT_VALIDATE_WORKTREE_MISSING_OK (1 << 0)

/*
 * Return zero if the worktree is in good condition. Error message is
 * returned if "errmsg" is not NULL.
 */
extern int validate_worktree(const struct worktree *wt,
			     struct strbuf *errmsg,
			     unsigned flags);

/*
 * Update worktrees/xxx/gitdir with the new path.
 */
extern void update_worktree_location(struct worktree *wt,
				     const char *path_);

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

/*
 * Similar to head_ref() for all HEADs _except_ one from the current
 * worktree, which is covered by head_ref().
 */
int other_head_refs(each_ref_fn fn, void *cb_data);

int is_worktree_being_rebased(const struct worktree *wt, const char *target);
int is_worktree_being_bisected(const struct worktree *wt, const char *target);

/*
 * Similar to git_path() but can produce paths for a specified
 * worktree instead of current one
 */
extern const char *worktree_git_path(const struct worktree *wt,
				     const char *fmt, ...)
	__attribute__((format (printf, 2, 3)));

/*
 * Parse a worktree ref (i.e. with prefix main-worktree/ or
 * worktrees/) and return the position of the worktree's name and
 * length (or NULL and zero if it's main worktree), and ref.
 *
 * All name, name_length and ref arguments could be NULL.
 */
int parse_worktree_ref(const char *worktree_ref, const char **name,
		       int *name_length, const char **ref);

/*
 * Return a refname suitable for access from the current ref store.
 */
void strbuf_worktree_ref(const struct worktree *wt,
			 struct strbuf *sb,
			 const char *refname);

/*
 * Return a refname suitable for access from the current ref
 * store. The result will be destroyed at the next call.
 */
const char *worktree_ref(const struct worktree *wt,
			 const char *refname);

#endif
