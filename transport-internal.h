#ifndef TRANSPORT_INTERNAL_H
#define TRANSPORT_INTERNAL_H

struct ref;
struct transport;
struct argv_array;

struct transport_vtable {
	/**
	 * This transport supports the fetch() function being called
	 * without get_refs_list() first being called.
	 */
	unsigned fetch_without_list : 1;

	/**
	 * Returns 0 if successful, positive if the option is not
	 * recognized or is inapplicable, and negative if the option
	 * is applicable but the value is invalid.
	 **/
	int (*set_option)(struct transport *connection, const char *name,
			  const char *value);
	/**
	 * Returns a list of the remote side's refs. In order to allow
	 * the transport to try to share connections, for_push is a
	 * hint as to whether the ultimate operation is a push or a fetch.
	 *
	 * If communicating using protocol v2 a list of prefixes can be
	 * provided to be sent to the server to enable it to limit the ref
	 * advertisement.  Since ref filtering is done on the server's end, and
	 * only when using protocol v2, this list will be ignored when not
	 * using protocol v2 meaning this function can return refs which don't
	 * match the provided ref_prefixes.
	 *
	 * If the transport is able to determine the remote hash for
	 * the ref without a huge amount of effort, it should store it
	 * in the ref's old_sha1 field; otherwise it should be all 0.
	 **/
	struct ref *(*get_refs_list)(struct transport *transport, int for_push,
				     const struct argv_array *ref_prefixes);

	/**
	 * Fetch the objects for the given refs. Note that this gets
	 * an array, and should ignore the list structure.
	 *
	 * If the transport did not get hashes for refs in
	 * get_refs_list(), it should set the old_sha1 fields in the
	 * provided refs now.
	 **/
	int (*fetch)(struct transport *transport, int refs_nr, struct ref **refs);

	/**
	 * Push the objects and refs. Send the necessary objects, and
	 * then, for any refs where peer_ref is set and
	 * peer_ref->new_oid is different from old_oid, tell the
	 * remote side to update each ref in the list from old_oid to
	 * peer_ref->new_oid.
	 *
	 * Where possible, set the status for each ref appropriately.
	 *
	 * The transport must modify new_sha1 in the ref to the new
	 * value if the remote accepted the change. Note that this
	 * could be a different value from peer_ref->new_oid if the
	 * process involved generating new commits.
	 **/
	int (*push_refs)(struct transport *transport, struct ref *refs, int flags);
	int (*connect)(struct transport *connection, const char *name,
		       const char *executable, int fd[2]);

	/** get_refs_list(), fetch(), and push_refs() can keep
	 * resources (such as a connection) reserved for further
	 * use. disconnect() releases these resources.
	 **/
	int (*disconnect)(struct transport *connection);
};

#endif
