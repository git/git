#ifndef PROMISOR_REMOTE_H
#define PROMISOR_REMOTE_H

#include "repository.h"

struct object_id;

/*
 * Promisor remote linked list
 *
 * Information in its fields come from remote.XXX config entries or
 * from extensions.partialclone, except for 'accepted' which comes
 * from protocol v2 capabilities exchange.
 */
struct promisor_remote {
	struct promisor_remote *next;
	char *partial_clone_filter;
	unsigned int accepted : 1;
	const char name[FLEX_ARRAY];
};

void repo_promisor_remote_reinit(struct repository *r);
void promisor_remote_clear(struct promisor_remote_config *config);
struct promisor_remote *repo_promisor_remote_find(struct repository *r, const char *remote_name);
int repo_has_promisor_remote(struct repository *r);

/*
 * Fetches all requested objects from all promisor remotes, trying them one at
 * a time until all objects are fetched.
 *
 * If oid_nr is 0, this function returns immediately.
 */
void promisor_remote_get_direct(struct repository *repo,
				const struct object_id *oids,
				int oid_nr);

/*
 * Append promisor remote info to buf. Useful for a server to
 * advertise the promisor remotes it uses.
 */
void promisor_remote_info(struct repository *repo, struct strbuf *buf);

/*
 * Prepare a reply to a "promisor-remote" advertisement from a server.
 */
char *promisor_remote_reply(const char *info);

/*
 * Set the 'accepted' flag for some promisor remotes. Useful when some
 * promisor remotes have been accepted by the client.
 */
void mark_promisor_remotes_as_accepted(struct repository *repo, const char *remotes);

/*
 * Has any promisor remote been accepted by the client?
 */
int repo_has_accepted_promisor_remote(struct repository *r);

#endif /* PROMISOR_REMOTE_H */
