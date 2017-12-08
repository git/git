#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "cache.h"
#include "run-command.h"
#include "remote.h"
#include "list-objects-filter-options.h"

struct string_list;

struct git_transport_options {
	unsigned thin : 1;
	unsigned keep : 1;
	unsigned followtags : 1;
	unsigned check_self_contained_and_connected : 1;
	unsigned self_contained_and_connected : 1;
	unsigned update_shallow : 1;
	unsigned deepen_relative : 1;
	unsigned from_promisor : 1;
	unsigned no_dependents : 1;
	int depth;
	const char *deepen_since;
	const struct string_list *deepen_not;
	const char *uploadpack;
	const char *receivepack;
	struct push_cas_option *cas;
	struct list_objects_filter_options filter_options;
};

enum transport_family {
	TRANSPORT_FAMILY_ALL = 0,
	TRANSPORT_FAMILY_IPV4,
	TRANSPORT_FAMILY_IPV6
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

	/*
	 * Transports that call take-over destroys the data specific to
	 * the transport type while doing so, and cannot be reused.
	 */
	unsigned cannot_reuse : 1;

	/*
	 * A hint from caller that it will be performing a clone, not
	 * normal fetch. IOW the repository is guaranteed empty.
	 */
	unsigned cloning : 1;

	/*
	 * These strings will be passed to the {pre, post}-receive hook,
	 * on the remote side, if both sides support the push options capability.
	 */
	const struct string_list *push_options;

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
	int (*push)(struct transport *connection, int refspec_nr, const char **refspec, int flags);
	int (*connect)(struct transport *connection, const char *name,
		       const char *executable, int fd[2]);

	/** get_refs_list(), fetch(), and push_refs() can keep
	 * resources (such as a connection) reserved for further
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

	enum transport_family family;
};

#define TRANSPORT_PUSH_ALL			(1<<0)
#define TRANSPORT_PUSH_FORCE			(1<<1)
#define TRANSPORT_PUSH_DRY_RUN			(1<<2)
#define TRANSPORT_PUSH_MIRROR			(1<<3)
#define TRANSPORT_PUSH_PORCELAIN		(1<<4)
#define TRANSPORT_PUSH_SET_UPSTREAM		(1<<5)
#define TRANSPORT_RECURSE_SUBMODULES_CHECK	(1<<6)
#define TRANSPORT_PUSH_PRUNE			(1<<7)
#define TRANSPORT_RECURSE_SUBMODULES_ON_DEMAND	(1<<8)
#define TRANSPORT_PUSH_NO_HOOK			(1<<9)
#define TRANSPORT_PUSH_FOLLOW_TAGS		(1<<10)
#define TRANSPORT_PUSH_CERT_ALWAYS		(1<<11)
#define TRANSPORT_PUSH_CERT_IF_ASKED		(1<<12)
#define TRANSPORT_PUSH_ATOMIC			(1<<13)
#define TRANSPORT_PUSH_OPTIONS			(1<<14)
#define TRANSPORT_RECURSE_SUBMODULES_ONLY	(1<<15)

extern int transport_summary_width(const struct ref *refs);

/* Returns a transport suitable for the url */
struct transport *transport_get(struct remote *, const char *);

/*
 * Check whether a transport is allowed by the environment.
 *
 * Type should generally be the URL scheme, as described in
 * Documentation/git.txt
 *
 * from_user specifies if the transport was given by the user.  If unknown pass
 * a -1 to read from the environment to determine if the transport was given by
 * the user.
 *
 */
int is_transport_allowed(const char *type, int from_user);

/*
 * Check whether a transport is allowed by the environment,
 * and die otherwise.
 */
void transport_check_allowed(const char *type);

/* Transport options which apply to git:// and scp-style URLs */

/* The program to use on the remote side to send a pack */
#define TRANS_OPT_UPLOADPACK "uploadpack"

/* The program to use on the remote side to receive a pack */
#define TRANS_OPT_RECEIVEPACK "receivepack"

/* Transfer the data as a thin pack if not null */
#define TRANS_OPT_THIN "thin"

/* Check the current value of the remote ref */
#define TRANS_OPT_CAS "cas"

/* Keep the pack that was transferred if not null */
#define TRANS_OPT_KEEP "keep"

/* Limit the depth of the fetch if not null */
#define TRANS_OPT_DEPTH "depth"

/* Limit the depth of the fetch based on time if not null */
#define TRANS_OPT_DEEPEN_SINCE "deepen-since"

/* Limit the depth of the fetch based on revs if not null */
#define TRANS_OPT_DEEPEN_NOT "deepen-not"

/* Limit the deepen of the fetch if not null */
#define TRANS_OPT_DEEPEN_RELATIVE "deepen-relative"

/* Aggressively fetch annotated tags if possible */
#define TRANS_OPT_FOLLOWTAGS "followtags"

/* Accept refs that may update .git/shallow without --depth */
#define TRANS_OPT_UPDATE_SHALLOW "updateshallow"

/* Send push certificates */
#define TRANS_OPT_PUSH_CERT "pushcert"

/* Indicate that these objects are being fetched by a promisor */
#define TRANS_OPT_FROM_PROMISOR "from-promisor"

/*
 * Indicate that only the objects wanted need to be fetched, not their
 * dependents
 */
#define TRANS_OPT_NO_DEPENDENTS "no-dependents"

/* Filter objects for partial clone and fetch */
#define TRANS_OPT_LIST_OBJECTS_FILTER "filter"

/**
 * Returns 0 if the option was used, non-zero otherwise. Prints a
 * message to stderr if the option is not used.
 **/
int transport_set_option(struct transport *transport, const char *name,
			 const char *value);
void transport_set_verbosity(struct transport *transport, int verbosity,
	int force_progress);

#define REJECT_NON_FF_HEAD     0x01
#define REJECT_NON_FF_OTHER    0x02
#define REJECT_ALREADY_EXISTS  0x04
#define REJECT_FETCH_FIRST     0x08
#define REJECT_NEEDS_FORCE     0x10

int transport_push(struct transport *connection,
		   int refspec_nr, const char **refspec, int flags,
		   unsigned int * reject_reasons);

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

/* common methods used by transport.c and builtin/send-pack.c */
void transport_verify_remote_names(int nr_heads, const char **heads);

void transport_update_tracking_ref(struct remote *remote, struct ref *ref, int verbose);

int transport_refs_pushed(struct ref *ref);

void transport_print_push_status(const char *dest, struct ref *refs,
		  int verbose, int porcelain, unsigned int *reject_reasons);

typedef void alternate_ref_fn(const char *refname, const struct object_id *oid, void *);
extern void for_each_alternate_ref(alternate_ref_fn, void *);
#endif
