#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "cache.h"
#include "remote.h"

struct git_transport_options {
	unsigned thin : 1;
	unsigned keep : 1;
	unsigned followtags : 1;
	int depth;
	const char *uploadpack;
	const char *receivepack;
};

struct transport {
	struct remote *remote;
	const char *url;
	void *data;
	const struct ref *remote_refs;

	/**
	 * Indicates whether we already called get_refs_list(); set by
	 * transport.c::transport_get_remote_refs().
	 */
	unsigned got_remote_refs : 1;

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
	struct ref *(*get_refs_list)(struct transport *transport, int for_push);

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
	 * peer_ref->new_sha1 is different from old_sha1, tell the
	 * remote side to update each ref in the list from old_sha1 to
	 * peer_ref->new_sha1.
	 *
	 * Where possible, set the status for each ref appropriately.
	 *
	 * The transport must modify new_sha1 in the ref to the new
	 * value if the remote accepted the change. Note that this
	 * could be a different value from peer_ref->new_sha1 if the
	 * process involved generating new commits.
	 **/
	int (*push_refs)(struct transport *transport, struct ref *refs, int flags);
	int (*push)(struct transport *connection, int refspec_nr, const char **refspec, int flags);
	int (*connect)(struct transport *connection, const char *name,
		       const char *executable, int fd[2]);

	/** get_refs_list(), fetch(), and push_refs() can keep
	 * resources (such as a connection) reserved for futher
	 * use. disconnect() releases these resources.
	 **/
	int (*disconnect)(struct transport *connection);
	char *pack_lockfile;
	signed verbose : 3;
	/**
	 * Transports should not set this directly, and should use this
	 * value without having to check isatty(2), -q/--quiet
	 * (transport->verbose < 0), etc. - checking has already been done
	 * in transport_set_verbosity().
	 **/
	unsigned progress : 1;
	/*
	 * If transport is at least potentially smart, this points to
	 * git_transport_options structure to use in case transport
	 * actually turns out to be smart.
	 */
	struct git_transport_options *smart_options;
};

#define TRANSPORT_PUSH_ALL 1
#define TRANSPORT_PUSH_FORCE 2
#define TRANSPORT_PUSH_DRY_RUN 4
#define TRANSPORT_PUSH_MIRROR 8
#define TRANSPORT_PUSH_PORCELAIN 16
#define TRANSPORT_PUSH_SET_UPSTREAM 32
#define TRANSPORT_RECURSE_SUBMODULES_CHECK 64

#define TRANSPORT_SUMMARY_WIDTH (2 * DEFAULT_ABBREV + 3)

/* Returns a transport suitable for the url */
struct transport *transport_get(struct remote *, const char *);

/* Transport options which apply to git:// and scp-style URLs */

/* The program to use on the remote side to send a pack */
#define TRANS_OPT_UPLOADPACK "uploadpack"

/* The program to use on the remote side to receive a pack */
#define TRANS_OPT_RECEIVEPACK "receivepack"

/* Transfer the data as a thin pack if not null */
#define TRANS_OPT_THIN "thin"

/* Keep the pack that was transferred if not null */
#define TRANS_OPT_KEEP "keep"

/* Limit the depth of the fetch if not null */
#define TRANS_OPT_DEPTH "depth"

/* Aggressively fetch annotated tags if possible */
#define TRANS_OPT_FOLLOWTAGS "followtags"

/**
 * Returns 0 if the option was used, non-zero otherwise. Prints a
 * message to stderr if the option is not used.
 **/
int transport_set_option(struct transport *transport, const char *name,
			 const char *value);
void transport_set_verbosity(struct transport *transport, int verbosity,
	int force_progress);

int transport_push(struct transport *connection,
		   int refspec_nr, const char **refspec, int flags,
		   int * nonfastforward);

const struct ref *transport_get_remote_refs(struct transport *transport);

int transport_fetch_refs(struct transport *transport, struct ref *refs);
void transport_unlock_pack(struct transport *transport);
int transport_disconnect(struct transport *transport);
char *transport_anonymize_url(const char *url);
void transport_take_over(struct transport *transport,
			 struct child_process *child);

int transport_connect(struct transport *transport, const char *name,
		      const char *exec, int fd[2]);

/* Transport methods defined outside transport.c */
int transport_helper_init(struct transport *transport, const char *name);
int bidirectional_transfer_loop(int input, int output);

/* common methods used by transport.c and builtin-send-pack.c */
void transport_verify_remote_names(int nr_heads, const char **heads);

void transport_update_tracking_ref(struct remote *remote, struct ref *ref, int verbose);

int transport_refs_pushed(struct ref *ref);

void transport_print_push_status(const char *dest, struct ref *refs,
		  int verbose, int porcelain, int *nonfastforward);

typedef void alternate_ref_fn(const struct ref *, void *);
extern void for_each_alternate_ref(alternate_ref_fn, void *);

#endif
