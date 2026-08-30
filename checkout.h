#ifndef CHECKOUT_H
#define CHECKOUT_H

#include "hash.h"

struct commit;
struct repository;

/*
 * Check if the branch name uniquely matches a branch name on a remote
 * tracking branch.  Return the name of the remote if such a branch
 * exists, NULL otherwise.
 */
char *unique_tracking_name(const char *name,
			   struct object_id *oid,
			   int *dwim_remotes_matched);

/*
 * Run the post-checkout hook.
 */
int post_checkout_hook(struct repository *,
		       struct commit *old_commit, struct commit *new_commit,
		       int changed);

#endif /* CHECKOUT_H */
