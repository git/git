#ifndef RESET_H
#define RESET_H

#include "hash.h"
#include "repository.h"

#define GIT_REFLOG_ACTION_ENVIRONMENT "GIT_REFLOG_ACTION"

/* Request a detached checkout */
#define RESET_HEAD_DETACH (1<<0)
/* Request a reset rather than a checkout */
#define RESET_HEAD_HARD (1<<1)
/* Run the post-checkout hook */
#define RESET_HEAD_RUN_POST_CHECKOUT_HOOK (1<<2)
/* Only update refs, do not touch the worktree */
#define RESET_HEAD_REFS_ONLY (1<<3)
/* Update ORIG_HEAD as well as HEAD */
#define RESET_ORIG_HEAD (1<<4)

struct reset_head_opts {
	/*
	 * The commit to checkout/reset to. Defaults to HEAD.
	 */
	const struct object_id *oid;
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
	unsigned flags;
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
	 * contains RESET_ORIG_HEAD then default_reflog_action must be given.
	 */
	const char *orig_head_msg;
	/*
	 * Action to use in default reflog messages, only required if a ref is
	 * being updated and the reflog messages above are omitted.
	 */
	const char *default_reflog_action;
};

int reset_head(struct repository *r, const struct reset_head_opts *opts);

#endif
