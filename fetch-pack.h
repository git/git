#ifndef FETCH_PACK_H
#define FETCH_PACK_H

#include "string-list.h"
#include "run-command.h"
#include "protocol.h"
#include "list-objects-filter-options.h"

struct oid_array;

struct fetch_pack_args {
	const char *uploadpack;
	int unpacklimit;
	int depth;
	const char *deepen_since;
	const struct string_list *deepen_not;
	struct list_objects_filter_options filter_options;
	const struct string_list *server_options;

	/*
	 * If not NULL, during packfile negotiation, fetch-pack will send "have"
	 * lines only with these tips and their ancestors.
	 */
	const struct oid_array *negotiation_tips;

	unsigned deepen_relative:1;
	unsigned quiet:1;
	unsigned keep_pack:1;
	unsigned lock_pack:1;
	unsigned use_thin_pack:1;
	unsigned fetch_all:1;
	unsigned stdin_refs:1;
	unsigned diag_url:1;
	unsigned verbose:1;
	unsigned no_progress:1;
	unsigned include_tag:1;
	unsigned stateless_rpc:1;
	unsigned check_self_contained_and_connected:1;
	unsigned self_contained_and_connected:1;
	unsigned cloning:1;
	unsigned update_shallow:1;
	unsigned deepen:1;
	unsigned from_promisor:1;

	/*
	 * Attempt to fetch only the wanted objects, and not any objects
	 * referred to by them. Due to protocol limitations, extraneous
	 * objects may still be included. (When fetching non-blob
	 * objects, only blobs are excluded; when fetching a blob, the
	 * blob itself will still be sent. The client does not need to
	 * know whether a wanted object is a blob or not.)
	 *
	 * If 1, fetch_pack() will also not modify any object flags.
	 * This allows fetch_pack() to safely be called by any function,
	 * regardless of which object flags it uses (if any).
	 */
	unsigned no_dependents:1;

	/*
	 * Because fetch_pack() overwrites the shallow file upon a
	 * successful deepening non-clone fetch, if this struct
	 * specifies such a fetch, fetch_pack() needs to perform a
	 * connectivity check before deciding if a fetch is successful
	 * (and overwriting the shallow file). fetch_pack() sets this
	 * field to 1 if such a connectivity check was performed.
	 *
	 * This is different from check_self_contained_and_connected
	 * in that the former allows existing objects in the
	 * repository to satisfy connectivity needs, whereas the
	 * latter doesn't.
	 */
	unsigned connectivity_checked:1;
};

/*
 * sought represents remote references that should be updated from.
 * On return, the names that were found on the remote will have been
 * marked as such.
 */
struct ref *fetch_pack(struct fetch_pack_args *args,
		       int fd[], struct child_process *conn,
		       const struct ref *ref,
		       const char *dest,
		       struct ref **sought,
		       int nr_sought,
		       struct oid_array *shallow,
		       char **pack_lockfile,
		       enum protocol_version version);

/*
 * Print an appropriate error message for each sought ref that wasn't
 * matched.  Return 0 if all sought refs were matched, otherwise 1.
 */
int report_unmatched_refs(struct ref **sought, int nr_sought);

#endif
