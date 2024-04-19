#ifndef BRANCH_H
#define BRANCH_H

struct repository;
struct strbuf;

enum branch_track {
	BRANCH_TRACK_UNSPECIFIED = -1,
	BRANCH_TRACK_NEVER = 0,
	BRANCH_TRACK_REMOTE,
	BRANCH_TRACK_ALWAYS,
	BRANCH_TRACK_EXPLICIT,
	BRANCH_TRACK_OVERRIDE,
	BRANCH_TRACK_INHERIT,
	BRANCH_TRACK_SIMPLE,
};

extern enum branch_track git_branch_track;

/* Functions for acting on the information about branches. */

/**
 * Sets branch.<new_ref>.{remote,merge} config settings such that
 * new_ref tracks orig_ref according to the specified tracking mode.
 *
 *   - new_ref is the name of the branch that we are setting tracking
 *     for.
 *
 *   - orig_ref is the name of the ref that is 'upstream' of new_ref.
 *     orig_ref will be expanded with DWIM so that the config settings
 *     are in the correct format e.g. "refs/remotes/origin/main" instead
 *     of "origin/main".
 *
 *   - track is the tracking mode e.g. BRANCH_TRACK_REMOTE causes
 *     new_ref to track orig_ref directly, whereas BRANCH_TRACK_INHERIT
 *     causes new_ref to track whatever orig_ref tracks.
 *
 *   - quiet suppresses tracking information.
 */
void dwim_and_setup_tracking(struct repository *r, const char *new_ref,
			     const char *orig_ref, enum branch_track track,
			     int quiet);

/*
 * Creates a new branch, where:
 *
 *   - r is the repository to add a branch to
 *
 *   - name is the new branch name
 *
 *   - start_name is the name of the existing branch that the new branch should
 *     start from
 *
 *   - force enables overwriting an existing (non-head) branch
 *
 *   - clobber_head_ok, when enabled with 'force', allows the currently
 *     checked out (head) branch to be overwritten
 *
 *   - reflog creates a reflog for the branch
 *
 *   - quiet suppresses tracking information
 *
 *   - track causes the new branch to be configured to merge the remote branch
 *     that start_name is a tracking branch for (if any).
 *
 *   - dry_run causes the branch to be validated but not created.
 *
 */
void create_branch(struct repository *r,
		   const char *name, const char *start_name,
		   int force, int clobber_head_ok,
		   int reflog, int quiet, enum branch_track track,
		   int dry_run);

/*
 * Creates a new branch in a repository and its submodules (and its
 * submodules, recursively). The parameters are mostly analogous to
 * those of create_branch() except for start_name, which is represented
 * by two different parameters:
 *
 * - start_committish is the commit-ish, in repository r, that determines
 *   which commits the branches will point to. The superproject branch
 *   will point to the commit of start_committish and the submodule
 *   branches will point to the gitlink commit oids in start_committish's
 *   tree.
 *
 * - tracking_name is the name of the ref, in repository r, that will be
 *   used to set up tracking information. This value is propagated to
 *   all submodules, which will evaluate the ref using their own ref
 *   stores. If NULL, this defaults to start_committish.
 *
 * When this function is called on the superproject, start_committish
 * can be any user-provided ref and tracking_name can be NULL (similar
 * to create_branches()). But when recursing through submodules,
 * start_committish is the plain gitlink commit oid. Since the oid cannot
 * be used for tracking information, tracking_name is propagated and
 * used for tracking instead.
 */
void create_branches_recursively(struct repository *r, const char *name,
				 const char *start_committish,
				 const char *tracking_name, int force,
				 int reflog, int quiet, enum branch_track track,
				 int dry_run);

/*
 * If the branch at 'refname' is currently checked out in a worktree,
 * then return the path to that worktree.
 */
const char *branch_checked_out(const char *refname);

/*
 * Check if 'name' can be a valid name for a branch; die otherwise.
 * Return 1 if the named branch already exists; return 0 otherwise.
 * Fill ref with the full refname for the branch.
 */
int validate_branchname(const char *name, struct strbuf *ref);

/*
 * Check if a branch 'name' can be created as a new branch; die otherwise.
 * 'force' can be used when it is OK for the named branch already exists.
 * Return 1 if the named branch already exists; return 0 otherwise.
 * Fill ref with the full refname for the branch.
 */
int validate_new_branchname(const char *name, struct strbuf *ref, int force);

/*
 * Remove information about the merge state on the current
 * branch. (E.g., MERGE_HEAD)
 */
void remove_merge_branch_state(struct repository *r);

/*
 * Remove information about the state of working on the current
 * branch. (E.g., MERGE_HEAD)
 */
void remove_branch_state(struct repository *r, int verbose);

/*
 * Configure local branch "local" as downstream to branch "remote"
 * from remote "origin".  Used by git branch --set-upstream.
 * Returns 0 on success.
 */
#define BRANCH_CONFIG_VERBOSE 01
int install_branch_config(int flag, const char *local, const char *origin, const char *remote);

/*
 * Read branch description
 */
int read_branch_desc(struct strbuf *, const char *branch_name);

/*
 * Check if a branch is checked out in the main worktree or any linked
 * worktree and die (with a message describing its checkout location) if
 * it is.
 */
void die_if_checked_out(const char *branch, int ignore_current_worktree);

#endif
