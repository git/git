#ifndef RESET_H
#define RESET_H

#include "hash.h"
#include "repository.h"

#define GIT_REFLOG_ACTION_ENVIRONMENT "GIT_REFLOG_ACTION"

enum reset_working_tree_flags {
	/* Request a detached checkout */
	RESET_WORKING_TREE_DETACH = (1 << 0),

	/* Request a reset rather than a checkout */
	RESET_WORKING_TREE_HARD = (1 << 1),

	/* Run the post-checkout hook */
	RESET_WORKING_TREE_RUN_POST_CHECKOUT_HOOK = (1 << 2),

	/* Only update refs, do not touch the worktree */
	RESET_WORKING_TREE_REFS_ONLY = (1 << 3),

	/* Update HEAD */
	RESET_WORKING_TREE_UPDATE_HEAD = (1 << 4),

	/* Update ORIG_HEAD */
	RESET_WORKING_TREE_UPDATE_ORIG_HEAD = (1 << 5),

	/*
	 * Perform a dry-run by performing the operation without updating
	 * any user-visible state.
	 */
	RESET_WORKING_TREE_DRY_RUN = (1 << 6),
};

struct reset_working_tree_options {
	/*
	 * The commit to checkout/reset to. Defaults to HEAD.
	 */
	const struct object_id *oid;
	/*
	 * The commit to checkout/reset from when doing a two-way merge. This
	 * is used as one of the sides to merge.
	 */
	const struct object_id *oid_from;
	/*
	 * Optional value to set ORIG_HEAD. Defaults to HEAD.
	 */
	const struct object_id *orig_head;
	/*
	 * Optional branch to switch to.
	 */
	const char *branch;
	/*
	 * Flags defined above.
	 */
	enum reset_working_tree_flags flags;
	/*
	 * Optional reflog message for branch, defaults to head_msg.
	 */
	const char *branch_msg;
	/*
	 * Optional reflog message for HEAD, if this omitted but oid or branch
	 * are given then default_reflog_action must be given.
	 */
	const char *head_msg;
	/*
	 * Optional reflog message for ORIG_HEAD, if this omitted and flags
	 * contains RESET_WORKING_TREE_UPDATE_ORIG_HEAD then
	 * default_reflog_action must be given.
	 */
	const char *orig_head_msg;
	/*
	 * Action to use in default reflog messages, only required if a ref is
	 * being updated and the reflog messages above are omitted.
	 */
	const char *default_reflog_action;
};

int reset_working_tree(struct repository *r, const struct reset_working_tree_options *opts);

#endif
