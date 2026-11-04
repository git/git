#ifndef CHECKOUT_H
#define CHECKOUT_H

#include "hash.h"

struct repository;
struct string_list;

/*
 * Check if the branch name uniquely matches a branch name on a remote
 * tracking branch.  Return the name of the remote if such a branch
 * exists, NULL otherwise.
 */
char *unique_tracking_name(struct repository *repo,
			   const char *name,
			   struct object_id *oid,
			   int *dwim_remotes_matched,
			   struct string_list *dwim_remote_names);

#endif /* CHECKOUT_H */
