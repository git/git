#ifndef BRANCH_H
#define BRANCH_H

/* Functions for acting on the information about branches. */

/*
 * Creates a new branch, where head is the branch currently checked
 * out, name is the new branch name, start_name is the name of the
 * existing branch that the new branch should start from, force
 * enables overwriting an existing (non-head) branch, reflog creates a
 * reflog for the branch, and track causes the new branch to be
 * configured to merge the remote branch that start_name is a tracking
 * branch for (if any).
 */
void create_branch(const char *head, const char *name, const char *start_name,
		   int force, int reflog, enum branch_track track);

/*
 * Validates that the requested branch may be created, returning the
 * interpreted ref in ref, force indicates whether (non-head) branches
 * may be overwritten. A non-zero return value indicates that the force
 * parameter was non-zero and the branch already exists.
 */
int validate_new_branchname(const char *name, struct strbuf *ref, int force);

/*
 * Remove information about the state of working on the current
 * branch. (E.g., MERGE_HEAD)
 */
void remove_branch_state(void);

/*
 * Configure local branch "local" as downstream to branch "remote"
 * from remote "origin".  Used by git branch --set-upstream.
 */
#define BRANCH_CONFIG_VERBOSE 01
extern void install_branch_config(int flag, const char *local, const char *origin, const char *remote);

#endif
