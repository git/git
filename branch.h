#ifndef BRANCH_H
#define BRANCH_H

/* Functions for acting on the information about branches. */

/*
 * Creates a new branch, where:
 *
 *   - name is the new branch name
 *
 *   - start_name is the name of the existing branch that the new branch should
 *     start from
 *
 *   - force enables overwriting an existing (non-head) branch
 *
 *   - reflog creates a reflog for the branch
 *
 *   - if 'force' is true, clobber_head indicates whether the branch could be
 *     the current branch else it has no effect
 *
 *   - quiet suppresses tracking information
 *
 *   - track causes the new branch to be configured to merge the remote branch
 *     that start_name is a tracking branch for (if any).
 */
void create_branch(const char *name, const char *start_name,
		   int force, int reflog,
		   int clobber_head, int quiet, enum branch_track track);

enum branch_validation_result {
	/* Flags that say it's NOT OK to update */
	BRANCH_EXISTS = -3,
	CANNOT_FORCE_UPDATE_CURRENT_BRANCH,
	INVALID_BRANCH_NAME,
	/* Flags that say it's OK to update */
	VALID_BRANCH_NAME = 0,
	FORCE_UPDATING_BRANCH = 1
};

/*
 * Validates whether the branch with the given name may be updated (created, renamed etc.,)
 * with respect to the given conditions. It returns the interpreted ref in ref.
 *
 * could_exist indicates whether the branch could exist or not.
 *
 * if 'could_exist' is true, clobber_head indicates whether the branch could be the
 * current branch else it has no effect.
 *
 * The return values have the following meaning,
 *
 *   - If dont_fail is 0, the function dies in case of failure and returns flags of
 *     'validate_result' that specify it is OK to update the branch. The positive
 *     non-zero flag implies that the branch can be force updated.
 *
 *   - If dont_fail is 1, the function doesn't die in case of failure but returns flags
 *     of 'validate_result' that specify the reason for failure. The behaviour in case of
 *     success is same as above.
 *
 */
int validate_branch_update(const char *name, struct strbuf *ref, int could_exist,
			   int clobber_head, unsigned dont_fail);

/*
 * Remove information about the state of working on the current
 * branch. (E.g., MERGE_HEAD)
 */
void remove_branch_state(void);

/*
 * Configure local branch "local" as downstream to branch "remote"
 * from remote "origin".  Used by git branch --set-upstream.
 * Returns 0 on success.
 */
#define BRANCH_CONFIG_VERBOSE 01
extern int install_branch_config(int flag, const char *local, const char *origin, const char *remote);

/*
 * Read branch description
 */
extern int read_branch_desc(struct strbuf *, const char *branch_name);

/*
 * Check if a branch is checked out in the main worktree or any linked
 * worktree and die (with a message describing its checkout location) if
 * it is.
 */
extern void die_if_checked_out(const char *branch, int ignore_current_worktree);

/*
 * Update all per-worktree HEADs pointing at the old ref to point the new ref.
 * This will be used when renaming a branch. Returns 0 if successful, non-zero
 * otherwise.
 */
extern int replace_each_worktree_head_symref(const char *oldref, const char *newref,
					     const char *logmsg);

#endif
