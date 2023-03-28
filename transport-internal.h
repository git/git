#ifndef TRANSPORT_INTERNAL_H
#define TRANSPORT_INTERNAL_H

struct ref;
struct transport;
struct strvec;
struct transport_ls_refs_options;

struct transport_vtable {
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
	 * If the transport is able to determine the remote hash for
	 * the ref without a huge amount of effort, it should store it
	 * in the ref's old_sha1 field; otherwise it should be all 0.
	 **/
	struct ref *(*get_refs_list)(struct transport *transport, int for_push,
				     struct transport_ls_refs_options *transport_options);

	/**
	 * Populates the remote side's bundle-uri under protocol v2,
	 * if the "bundle-uri" capability was advertised. Returns 0 if
	 * OK, negative values on error.
	 */
	int (*get_bundle_uri)(struct transport *transport);

	/**
	 * Fetch the objects for the given refs. Note that this gets
	 * an array, and should ignore the list structure.
	 *
	 * If the transport did not get hashes for refs in
	 * get_refs_list(), it should set the old_sha1 fields in the
	 * provided refs now.
	 **/
	int (*fetch_refs)(struct transport *transport, int refs_nr, struct ref **refs);

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
