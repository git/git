#ifndef FETCH_PACK_H
#define FETCH_PACK_H

#include "string-list.h"
#include "run-command.h"
#include "protocol.h"
#include "list-objects-filter-options.h"
#include "oidset.h"

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
	unsigned reject_shallow_remote:1;
	unsigned deepen:1;
	unsigned refetch:1;

	/*
	 * Indicate that the remote of this request is a promisor remote. The
	 * pack received does not need all referred-to objects to be present in
	 * the local object store, and fetch-pack will store the pack received
	 * together with a ".promisor" file indicating that the aforementioned
	 * pack is a promisor pack.
	 */
	unsigned from_promisor:1;

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
		       int fd[],
		       const struct ref *ref,
		       struct ref **sought,
		       int nr_sought,
		       struct oid_array *shallow,
		       struct string_list *pack_lockfiles,
		       enum protocol_version version);

/*
 * Execute the --negotiate-only mode of "git fetch", adding all known common
 * commits to acked_commits.
 *
 * In the capability advertisement that has happened prior to invoking this
 * function, the "wait-for-done" capability must be present.
 */
void negotiate_using_fetch(const struct oid_array *negotiation_tips,
			   const struct string_list *server_options,
			   int stateless_rpc,
			   int fd[],
			   struct oidset *acked_commits);

/*
 * Print an appropriate error message for each sought ref that wasn't
 * matched.  Return 0 if all sought refs were matched, otherwise 1.
 */
int report_unmatched_refs(struct ref **sought, int nr_sought);

#endif
