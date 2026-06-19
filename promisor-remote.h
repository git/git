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
	char *advertised_filter;
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
 * Callers are responsible for filtering out OIDs that are already present
 * locally before calling this function: every supplied OID is sent in the
 * fetch request, even if the object already exists in the local object
 * store. (Only after a fetch failure does this function fall back to
 * stripping already-present OIDs from the list before trying the next
 * configured promisor remote.) Callers should also deduplicate the OIDs.
 *
 * To test for local presence without triggering a lazy fetch (which would
 * defeat the purpose of batching), use odb_has_object(..., 0) or
 * odb_read_object_info_extended() with OBJECT_INFO_FOR_PREFETCH.
 *
 * If oid_nr is 0, this function returns immediately.
 */
void promisor_remote_get_direct(struct repository *repo,
				const struct object_id *oids,
				int oid_nr);

/*
 * Prepare a "promisor-remote" advertisement by a server.
 * Check the value of "promisor.advertise" and maybe the configured
 * promisor remotes, if any, to prepare information to send in an
 * advertisement.
 * Return value is NULL if no promisor remote advertisement should be
 * made. Otherwise it contains the names and urls of the advertised
 * promisor remotes separated by ';'. See gitprotocol-v2(5).
 */
char *promisor_remote_info(struct repository *repo);

/*
 * Prepare a reply to a "promisor-remote" advertisement from a server.
 * Check the value of "promisor.acceptfromserver" and maybe the
 * configured promisor remotes, if any, to prepare the reply. If the
 * `accepted_out` argument is not NULL, it is set to either NULL or to
 * the names of the accepted promisor remotes separated by ';' if
 * any. See gitprotocol-v2(5).
 */
void promisor_remote_reply(const char *info, char **accepted_out);

/*
 * Set the 'accepted' flag for some promisor remotes. Useful on the
 * server side when some promisor remotes have been accepted by the
 * client.
 */
void mark_promisor_remotes_as_accepted(struct repository *repo, const char *remotes);

/*
 * Has any promisor remote been accepted by the client?
 */
int repo_has_accepted_promisor_remote(struct repository *r);

/*
 * Use the filters from the accepted remotes to create a combined
 * filter (useful in `--filter=auto` mode).
 */
char *promisor_remote_construct_filter(struct repository *repo);

#endif /* PROMISOR_REMOTE_H */
