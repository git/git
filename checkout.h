#ifndef CHECKOUT_H
#define CHECKOUT_H

#include "hash-ll.h"

/*
 * Check if the branch name uniquely matches a branch name on a remote
 * tracking branch.  Return the name of the remote if such a branch
 * exists, NULL otherwise.
 */
const char *unique_tracking_name(const char *name,
				 struct object_id *oid,
				 int *dwim_remotes_matched);

#endif /* CHECKOUT_H */
