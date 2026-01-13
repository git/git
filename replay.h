#ifndef REPLAY_H
#define REPLAY_H

#include "hash.h"

struct repository;
struct rev_info;

/*
 * A set of options that can be passed to `replay_revisions()`.
 */
struct replay_revisions_options {
	/*
	 * Starting point at which to create the new commits; must be a branch
	 * name. The branch will be updated to point to the rewritten commits.
	 * This option is mutually exclusive with `onto`.
	 */
	const char *advance;

	/*
	 * Starting point at which to create the new commits; must be a
	 * committish. References pointing at decendants of `onto` will be
	 * updated to point to the new commits.
	 */
	 const char *onto;

	/*
	 * Update branches that point at commits in the given revision range.
	 * Requires `onto` to be set.
	 */
	int contained;
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
