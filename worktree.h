#ifndef WORKTREE_H
#define WORKTREE_H

#include "refs.h"

struct strbuf;

struct worktree {
	/* The repository this worktree belongs to. */
	struct repository *repo;
	char *path;
	char *id;
	char *head_ref;		/* NULL if HEAD is broken or detached */
	char *lock_reason;	/* private - use worktree_lock_reason */
	char *prune_reason;     /* private - use worktree_prune_reason */
	struct object_id head_oid;
	int is_detached;
	int is_bare;
	int is_current;
	int lock_reason_valid; /* private */
	int prune_reason_valid; /* private */
};

/*
 * Get the worktrees.  The primary worktree will always be the first returned,
 * and linked worktrees will follow in no particular order.
 *
 * The caller is responsible for freeing the memory from the returned
 * worktrees by calling free_worktrees().
 */
struct worktree **get_worktrees(void);

/*
 * Returns 1 if linked worktrees exist, 0 otherwise.
 */
int submodule_uses_worktrees(const char *path);

/*
 * Return git dir of the worktree. Note that the path may be relative.
 * If wt is NULL, git dir of current worktree is returned.
 */
const char *get_worktree_git_dir(const struct worktree *wt);

/*
 * Search for the worktree identified unambiguously by `arg` -- typically
 * supplied by the user via the command-line -- which may be a pathname or some
 * shorthand uniquely identifying a worktree, thus making it convenient for the
 * user to specify a worktree with minimal typing. For instance, if the last
 * component (say, "foo") of a worktree's pathname is unique among worktrees
 * (say, "work/foo" and "work/bar"), it can be used to identify the worktree
 * unambiguously.
 *
 * `prefix` should be the `prefix` handed to top-level Git commands along with
 * `argc` and `argv`.
 *
 * Return the worktree identified by `arg`, or NULL if not found.
 */
struct worktree *find_worktree(struct worktree **list,
			       const char *prefix,
			       const char *arg);

/*
 * Look up the worktree corresponding to `id`, or NULL of no such worktree
 * exists.
 */
struct worktree *get_linked_worktree(const char *id,
				     int skip_reading_head);

/*
 * Return the worktree corresponding to `path`, or NULL if no such worktree
 * exists.
 */
struct worktree *find_worktree_by_path(struct worktree **, const char *path);

/*
 * Return true if the given worktree is the main one.
 */
int is_main_worktree(const struct worktree *wt);

/*
 * Return the reason string if the given worktree is locked or NULL
 * otherwise.
 */
const char *worktree_lock_reason(struct worktree *wt);

/*
 * Return the reason string if the given worktree should be pruned, otherwise
 * NULL if it should not be pruned. `expire` defines a grace period to prune
 * the worktree when its path does not exist.
 */
const char *worktree_prune_reason(struct worktree *wt, timestamp_t expire);

/*
 * Return true if worktree entry should be pruned, along with the reason for
 * pruning. Otherwise, return false and the worktree's path in `wtpath`, or
 * NULL if it cannot be determined. Caller is responsible for freeing
 * returned path.
 *
 * `expire` defines a grace period to prune the worktree when its path
 * does not exist.
 */
int should_prune_worktree(const char *id,
			  struct strbuf *reason,
			  char **wtpath,
			  timestamp_t expire);

#define WT_VALIDATE_WORKTREE_MISSING_OK (1 << 0)

/*
 * Return zero if the worktree is in good condition. Error message is
 * returned if "errmsg" is not NULL.
 */
int validate_worktree(const struct worktree *wt,
		      struct strbuf *errmsg,
		      unsigned flags);

/*
 * Update worktrees/xxx/gitdir with the new path.
 */
void update_worktree_location(struct worktree *wt, const char *path_,
			      int use_relative_paths);

typedef void (* worktree_repair_fn)(int iserr, const char *path,
				    const char *msg, void *cb_data);

/*
 * Visit each registered linked worktree and repair corruptions. For each
 * repair made or error encountered while attempting a repair, the callback
 * function, if non-NULL, is called with the path of the worktree and a
 * description of the repair or error, along with the callback user-data.
 */
void repair_worktrees(worktree_repair_fn, void *cb_data, int use_relative_paths);

/*
 * Repair the linked worktrees after the gitdir has been moved.
 */
void repair_worktrees_after_gitdir_move(const char *old_path);

/*
 * Repair the linked worktree after the gitdir has been moved.
 */
void repair_worktree_after_gitdir_move(struct worktree *wt, const char *old_path);

/*
 * Repair administrative files corresponding to the worktree at the given path.
 * The worktree's .git file pointing at the repository must be intact for the
 * repair to succeed. Useful for re-associating an orphaned worktree with the
 * repository if the worktree has been moved manually (without using "git
 * worktree move"). For each repair made or error encountered while attempting
 * a repair, the callback function, if non-NULL, is called with the path of the
 * worktree and a description of the repair or error, along with the callback
 * user-data.
 */
void repair_worktree_at_path(const char *, worktree_repair_fn,
			     void *cb_data, int use_relative_paths);

/*
 * Free up the memory for a worktree.
 */
void free_worktree(struct worktree *);

/*
 * Free up the memory for worktree(s)
 */
void free_worktrees(struct worktree **);

/*
 * Check if a per-worktree symref points to a ref in the main worktree
 * or any linked worktree, and return the worktree that holds the ref,
 * or NULL otherwise.
 */
const struct worktree *find_shared_symref(struct worktree **worktrees,
					  const char *symref,
					  const char *target);

/*
 * Returns true if a symref points to a ref in a worktree.
 */
int is_shared_symref(const struct worktree *wt,
		     const char *symref, const char *target);

/*
 * Similar to head_ref() for all HEADs _except_ one from the current
 * worktree, which is covered by head_ref().
 */
int other_head_refs(each_ref_fn fn, void *cb_data);

int is_worktree_being_rebased(const struct worktree *wt, const char *target);
int is_worktree_being_bisected(const struct worktree *wt, const char *target);

/*
 * Return a refname suitable for access from the current ref store.
 */
void strbuf_worktree_ref(const struct worktree *wt,
			 struct strbuf *sb,
			 const char *refname);

/**
 * Enable worktree config for the first time. This will make the following
 * adjustments:
 *
 * 1. Add extensions.worktreeConfig=true in the common config file.
 *
 * 2. If the common config file has a core.worktree value, then that value
 *    is moved to the main worktree's config.worktree file.
 *
 * 3. If the common config file has a core.bare enabled, then that value
 *    is moved to the main worktree's config.worktree file.
 *
 * If extensions.worktreeConfig is already true, then this method
 * terminates early without any of the above steps. The existing config
 * arrangement is assumed to be intentional.
 *
 * Returns 0 on success. Reports an error message and returns non-zero
 * if any of these steps fail.
 */
int init_worktree_config(struct repository *r);

/**
 * Write the .git file and gitdir file that links the worktree to the repository.
 *
 * The `dotgit` parameter is the path to the worktree's .git file, and `gitdir`
 * is the path to the repository's `gitdir` file.
 *
 * Example
 *  dotgit: "/path/to/foo/.git"
 *  gitdir: "/path/to/repo/worktrees/foo/gitdir"
 */
void write_worktree_linking_files(struct strbuf dotgit, struct strbuf gitdir,
				  int use_relative_paths);

#endif
