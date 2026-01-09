#ifndef CHECKOUT_H
#define CHECKOUT_H

#include "hash.h"

/*
 * If checkout.remoteBranchTemplate is set, expand it using printf-style
 * substitution:
 *   %s -> the branch name
 *   %% -> a literal %
 * Returns a newly allocated string, or NULL if unset/invalid.
 */
char *expand_remote_branch_template(const char *name);

/*
 * Check if the branch name uniquely matches a branch name on a remote
 * tracking branch.  Return the name of the remote if such a branch
 * exists, NULL otherwise.
 */
char *unique_tracking_name(const char *name,
			   struct object_id *oid,
			   int *dwim_remotes_matched);

#endif /* CHECKOUT_H */
