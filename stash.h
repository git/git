#ifndef STASH_H
#define STASH_H

enum stash_apply_result {
	/* The stash was applied cleanly, or there was nothing to apply. */
	STASH_APPLY_CLEAN = 0,

	/*
	 * The stash could not be applied because it resulted in
	 * conflicts.  The stash entry is left in place.  The "git stash
	 * apply", "pop" and "branch" subcommands exit with this status
	 * in this case, mirroring the convention of "git merge-tree" and
	 * the merge strategies.
	 */
	STASH_APPLY_CONFLICT = 1,

	/* Something went wrong. */
	STASH_APPLY_ERROR = -1,
};

#endif /* STASH_H */
