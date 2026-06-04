#ifndef REPLAY_H
#define REPLAY_H

#include "hash.h"

struct repository;
struct rev_info;

/*
 * Controls what happens when a replayed commit becomes empty (i.e. its tree
 * is identical to its parent's tree after the replay).
 */
enum replay_empty_commit_action {
	/* Silently discard the empty commit. */
	REPLAY_EMPTY_COMMIT_DROP,
	/* Keep the empty commit as-is. */
	REPLAY_EMPTY_COMMIT_KEEP,
	/* Abort with an error. */
	REPLAY_EMPTY_COMMIT_ABORT,
};

/*
 * A set of options that can be passed to `replay_revisions()`.
 */
struct replay_revisions_options {
	/*
	 * Starting point at which to create the new commits; must be a branch
	 * name. The branch will be updated to point to the rewritten commits.
	 * This option is mutually exclusive with `onto` and `revert`.
	 */
	const char *advance;

	/*
	 * Starting point at which to create the new commits; must be a
	 * committish. References pointing at decendants of `onto` will be
	 * updated to point to the new commits.
	 */
	const char *onto;

	/*
	 * Reference to update with the result of the replay. This will not
	 * update any refs from `onto`, `advance`, or `revert`. Ignores
	 * `contained`.
	 */
	const char *ref;

	/*
	 * Starting point at which to create revert commits; must be a branch
	 * name. The branch will be updated to point to the revert commits.
	 * This option is mutually exclusive with `onto` and `advance`.
	 */
	const char *revert;

	/*
	 * Update branches that point at commits in the given revision range.
	 * Requires `onto` to be set.
	 */
	int contained;

	/*
	 * Controls what to do when a replayed commit becomes empty.
	 * Defaults to REPLAY_EMPTY_COMMIT_DROP.
	 */
	enum replay_empty_commit_action empty;
};

/* This struct is used as an out-parameter by `replay_revisions()`. */
struct replay_result {
	/*
	 * The set of reference updates that are caused by replaying the
	 * commits.
	 */
	struct replay_ref_update {
		char *refname;
		struct object_id old_oid;
		struct object_id new_oid;
	} *updates;
	size_t updates_nr, updates_alloc;
};

void replay_result_release(struct replay_result *result);

/*
 * Replay a set of commits onto a new location. Leaves both the working tree,
 * index and references untouched. Reference updates caused by the replay will
 * be recorded in the `updates` out pointer.
 *
 * Returns 0 on success, 1 on conflict and a negative error code otherwise.
 */
int replay_revisions(struct rev_info *revs,
		     struct replay_revisions_options *opts,
		     struct replay_result *out);

#endif
