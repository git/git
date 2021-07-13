#ifndef PROMISOR_REMOTE_H
#define PROMISOR_REMOTE_H

#include "repository.h"

struct object_id;

/*
 * Promisor remote linked list
 *
 * Information in its fields come from remote.XXX config entries or
 * from extensions.partialclone.
 */
struct promisor_remote {
	struct promisor_remote *next;
	const char *partial_clone_filter;
	const char name[FLEX_ARRAY];
};

void repo_promisor_remote_reinit(struct repository *r);
static inline void promisor_remote_reinit(void)
{
	repo_promisor_remote_reinit(the_repository);
}

void promisor_remote_clear(struct promisor_remote_config *config);

struct promisor_remote *repo_promisor_remote_find(struct repository *r, const char *remote_name);
static inline struct promisor_remote *promisor_remote_find(const char *remote_name)
{
	return repo_promisor_remote_find(the_repository, remote_name);
}

int repo_has_promisor_remote(struct repository *r);
static inline int has_promisor_remote(void)
{
	return repo_has_promisor_remote(the_repository);
}

/*
 * Fetches all requested objects from all promisor remotes, trying them one at
 * a time until all objects are fetched. Returns 0 upon success, and non-zero
 * otherwise.
 *
 * If oid_nr is 0, this function returns 0 (success) immediately.
 */
int promisor_remote_get_direct(struct repository *repo,
			       const struct object_id *oids,
			       int oid_nr);

#endif /* PROMISOR_REMOTE_H */
