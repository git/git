#ifndef REMOTE_H
#define REMOTE_H

#include "hash.h"
#include "hashmap.h"
#include "refspec.h"
#include "strvec.h"

struct option;
struct transport_ls_refs_options;

/**
 * The API gives access to the configuration related to remotes. It handles
 * all three configuration mechanisms historically and currently used by Git,
 * and presents the information in a uniform fashion. Note that the code also
 * handles plain URLs without any configuration, giving them just the default
 * information.
 */

enum {
	REMOTE_UNCONFIGURED = 0,
	REMOTE_CONFIG,
	REMOTE_REMOTES,
	REMOTE_BRANCHES
};

struct rewrite {
	const char *base;
	size_t baselen;
	struct counted_string *instead_of;
	int instead_of_nr;
	int instead_of_alloc;
};

struct rewrites {
	struct rewrite **rewrite;
	int rewrite_alloc;
	int rewrite_nr;
};

struct remote_state {
	struct remote **remotes;
	int remotes_alloc;
	int remotes_nr;
	struct hashmap remotes_hash;

	struct hashmap branches_hash;

	struct branch *current_branch;
	char *pushremote_name;

	struct rewrites rewrites;
	struct rewrites rewrites_push;

	int initialized;
};

void remote_state_clear(struct remote_state *remote_state);
struct remote_state *remote_state_new(void);

struct remote {
	struct hashmap_entry ent;

	/* The user's nickname for the remote */
	const char *name;

	int origin, configured_in_repo;

	char *foreign_vcs;

	/* An array of all of the url_nr URLs configured for the remote */
	struct strvec url;
	/* An array of all of the pushurl_nr push URLs configured for the remote */
	struct strvec pushurl;

	struct refspec push;

	struct refspec fetch;

	/*
	 * The setting for whether to fetch tags (as a separate rule from the
	 * configured refspecs);
	 * -1 to never fetch tags
	 * 0 to auto-follow tags on heuristic (default)
	 * 1 to always auto-follow tags
	 * 2 to always fetch tags
	 */
	int fetch_tags;

	int skip_default_update;
	int mirror;
	int prune;
	int prune_tags;

	/**
	 * The configured helper programs to run on the remote side, for
	 * Git-native protocols.
	 */
	const char *receivepack;
	const char *uploadpack;

	/* The proxy to use for curl (http, https, ftp, etc.) URLs. */
	char *http_proxy;

	/* The method used for authenticating against `http_proxy`. */
	char *http_proxy_authmethod;
};

/**
 * struct remotes can be found by name with remote_get().
 * remote_get(NULL) will return the default remote, given the current branch
 * and configuration.
 */
struct remote *remote_get(const char *name);
struct remote *remote_get_early(const char *name);

struct remote *pushremote_get(const char *name);
int remote_is_configured(struct remote *remote, int in_repo);

typedef int each_remote_fn(struct remote *remote, void *priv);

/* iterate through struct remotes */
int for_each_remote(each_remote_fn fn, void *priv);

int remote_has_url(struct remote *remote, const char *url);
struct strvec *push_url_of_remote(struct remote *remote);

struct ref_push_report {
	const char *ref_name;
	struct object_id *old_oid;
	struct object_id *new_oid;
	unsigned int forced_update:1;
	struct ref_push_report *next;
};

struct ref {
	struct ref *next;
	struct object_id old_oid;
	struct object_id new_oid;
	struct object_id old_oid_expect; /* used by expect-old */
	char *symref;
	char *tracking_ref;
	unsigned int
		force:1,
		forced_update:1,
		expect_old_sha1:1,
		exact_oid:1,
		deletion:1,
		/* Need to check if local reflog reaches the remote tip. */
		check_reachable:1,
		/*
		 * Store the result of the check enabled by "check_reachable";
		 * implies the local reflog does not reach the remote tip.
		 */
		unreachable:1;

	enum {
		REF_NOT_MATCHED = 0, /* initial value */
		REF_MATCHED,
		REF_UNADVERTISED_NOT_ALLOWED
	} match_status;

	/*
	 * Order is important here, as we write to FETCH_HEAD
	 * in numeric order. And the default NOT_FOR_MERGE
	 * should be 0, so that xcalloc'd structures get it
	 * by default.
	 */
	enum fetch_head_status {
		FETCH_HEAD_MERGE = -1,
		FETCH_HEAD_NOT_FOR_MERGE = 0,
		FETCH_HEAD_IGNORE = 1
	} fetch_head_status;

	enum {
		REF_STATUS_NONE = 0,
		REF_STATUS_OK,
		REF_STATUS_REJECT_NONFASTFORWARD,
		REF_STATUS_REJECT_ALREADY_EXISTS,
		REF_STATUS_REJECT_NODELETE,
		REF_STATUS_REJECT_FETCH_FIRST,
		REF_STATUS_REJECT_NEEDS_FORCE,
		REF_STATUS_REJECT_STALE,
		REF_STATUS_REJECT_SHALLOW,
		REF_STATUS_REJECT_REMOTE_UPDATED,
		REF_STATUS_UPTODATE,
		REF_STATUS_REMOTE_REJECT,
		REF_STATUS_EXPECTING_REPORT,
		REF_STATUS_ATOMIC_PUSH_FAILED
	} status;
	char *remote_status;
	struct ref_push_report *report;
	struct ref *peer_ref; /* when renaming */
	char name[FLEX_ARRAY]; /* more */
};

#define REF_NORMAL	(1u << 0)
#define REF_BRANCHES	(1u << 1)
#define REF_TAGS	(1u << 2)

struct ref *find_ref_by_name(const struct ref *list, const char *name);

struct ref *alloc_ref(const char *name);
struct ref *copy_ref(const struct ref *ref);
struct ref *copy_ref_list(const struct ref *ref);
int count_refspec_match(const char *, struct ref *refs, struct ref **matched_ref);

int check_ref_type(const struct ref *ref, int flags);

/*
 * Free a single ref and its peer, or an entire list of refs and their peers,
 * respectively.
 */
void free_one_ref(struct ref *ref);
void free_refs(struct ref *ref);

struct oid_array;
struct packet_reader;
struct strvec;
struct string_list;
struct ref **get_remote_heads(struct packet_reader *reader,
			      struct ref **list, unsigned int flags,
			      struct oid_array *extra_have,
			      struct oid_array *shallow_points);

/* Used for protocol v2 in order to retrieve refs from a remote */
struct ref **get_remote_refs(int fd_out, struct packet_reader *reader,
			     struct ref **list, int for_push,
			     struct transport_ls_refs_options *transport_options,
			     const struct string_list *server_options,
			     int stateless_rpc);

/* Used for protocol v2 in order to retrieve refs from a remote */
struct bundle_list;
int get_remote_bundle_uri(int fd_out, struct packet_reader *reader,
			  struct bundle_list *bundles, int stateless_rpc);

int resolve_remote_symref(struct ref *ref, struct ref *list);

/*
 * Remove and free all but the first of any entries in the input list
 * that map the same remote reference to the same local reference.  If
 * there are two entries that map different remote references to the
 * same local reference, emit an error message and die.  Return a
 * pointer to the head of the resulting list.
 */
struct ref *ref_remove_duplicates(struct ref *ref_map);

/*
 * Check whether a name matches any negative refspec in rs. Returns 1 if the
 * name matches at least one negative refspec, and 0 otherwise.
 */
int omit_name_by_refspec(const char *name, struct refspec *rs);

/*
 * Remove all entries in the input list which match any negative refspec in
 * the refspec list.
 */
struct ref *apply_negative_refspecs(struct ref *ref_map, struct refspec *rs);

int query_refspecs(struct refspec *rs, struct refspec_item *query);
char *apply_refspecs(struct refspec *rs, const char *name);

int check_push_refs(struct ref *src, struct refspec *rs);
int match_push_refs(struct ref *src, struct ref **dst,
		    struct refspec *rs, int flags);
void set_ref_status_for_push(struct ref *remote_refs, int send_mirror,
	int force_update);

/*
 * Given a list of the remote refs and the specification of things to
 * fetch, makes a (separate) list of the refs to fetch and the local
 * refs to store into. Note that negative refspecs are ignored here, and
 * should be handled separately.
 *
 * *tail is the pointer to the tail pointer of the list of results
 * beforehand, and will be set to the tail pointer of the list of
 * results afterward.
 *
 * missing_ok is usually false, but when we are adding branch.$name.merge
 * it is Ok if the branch is not at the remote anymore.
 */
int get_fetch_map(const struct ref *remote_refs, const struct refspec_item *refspec,
		  struct ref ***tail, int missing_ok);

struct ref *get_remote_ref(const struct ref *remote_refs, const char *name);

/*
 * For the given remote, reads the refspec's src and sets the other fields.
 */
int remote_find_tracking(struct remote *remote, struct refspec_item *refspec);

/**
 * struct branch holds the configuration for a branch. It can be looked up with
 * branch_get(name) for "refs/heads/{name}", or with branch_get(NULL) for HEAD.
 */
struct branch {
	struct hashmap_entry ent;

	/* The short name of the branch. */
	const char *name;

	/* The full path for the branch ref. */
	const char *refname;

	/* The name of the remote listed in the configuration. */
	char *remote_name;

	char *pushremote_name;

	/* An array of the "merge" lines in the configuration. */
	const char **merge_name;

	/**
	 * An array of the struct refspecs used for the merge lines. That is,
	 * merge[i]->dst is a local tracking ref which should be merged into this
	 * branch by default.
	 */
	struct refspec_item **merge;

	/* The number of merge configurations */
	int merge_nr;

	int merge_alloc;

	const char *push_tracking_ref;
};

struct branch *branch_get(const char *name);
const char *remote_for_branch(struct branch *branch, int *explicit);
const char *pushremote_for_branch(struct branch *branch, int *explicit);
char *remote_ref_for_branch(struct branch *branch, int for_push);

/* returns true if the given branch has merge configuration given. */
int branch_has_merge_config(struct branch *branch);

int branch_merge_matches(struct branch *, int n, const char *);

/**
 * Return the fully-qualified refname of the tracking branch for `branch`.
 * I.e., what "branch@{upstream}" would give you. Returns NULL if no
 * upstream is defined.
 *
 * If `err` is not NULL and no upstream is defined, a more specific error
 * message is recorded there (if the function does not return NULL, then
 * `err` is not touched).
 */
const char *branch_get_upstream(struct branch *branch, struct strbuf *err);

/**
 * Return the tracking branch that corresponds to the ref we would push to
 * given a bare `git push` while `branch` is checked out.
 *
 * The return value and `err` conventions match those of `branch_get_upstream`.
 */
const char *branch_get_push(struct branch *branch, struct strbuf *err);

/* Flags to match_refs. */
enum match_refs_flags {
	MATCH_REFS_NONE		= 0,
	MATCH_REFS_ALL 		= (1 << 0),
	MATCH_REFS_MIRROR	= (1 << 1),
	MATCH_REFS_PRUNE	= (1 << 2),
	MATCH_REFS_FOLLOW_TAGS	= (1 << 3)
};

/* Flags for --ahead-behind option. */
enum ahead_behind_flags {
	AHEAD_BEHIND_UNSPECIFIED = -1,
	AHEAD_BEHIND_QUICK       =  0,  /* just eq/neq reporting */
	AHEAD_BEHIND_FULL        =  1,  /* traditional a/b reporting */
};

/* Reporting of tracking info */
int stat_tracking_info(struct branch *branch, int *num_ours, int *num_theirs,
		       const char **upstream_name, int for_push,
		       enum ahead_behind_flags abf);
int format_tracking_info(struct branch *branch, struct strbuf *sb,
			 enum ahead_behind_flags abf,
			 int show_divergence_advice);

struct ref *get_local_heads(void);
/*
 * Find refs from a list which are likely to be pointed to by the given HEAD
 * ref. If 'all' is false, returns the most likely ref; otherwise, returns a
 * list of all candidate refs. If no match is found (or 'head' is NULL),
 * returns NULL. All returns are newly allocated and should be freed.
 */
struct ref *guess_remote_head(const struct ref *head,
			      const struct ref *refs,
			      int all);

/* Return refs which no longer exist on remote */
struct ref *get_stale_heads(struct refspec *rs, struct ref *fetch_map);

/*
 * Compare-and-swap
 */
struct push_cas_option {
	unsigned use_tracking_for_rest:1;
	unsigned use_force_if_includes:1;
	struct push_cas {
		struct object_id expect;
		unsigned use_tracking:1;
		char *refname;
	} *entry;
	int nr;
	int alloc;
};

int parseopt_push_cas_option(const struct option *, const char *arg, int unset);

int is_empty_cas(const struct push_cas_option *);
void apply_push_cas(struct push_cas_option *, struct remote *, struct ref *);

/*
 * The `url` argument is the URL that navigates to the submodule origin
 * repo. When relative, this URL is relative to the superproject origin
 * URL repo. The `up_path` argument, if specified, is the relative
 * path that navigates from the submodule working tree to the superproject
 * working tree. Returns the origin URL of the submodule.
 *
 * Return either an absolute URL or filesystem path (if the superproject
 * origin URL is an absolute URL or filesystem path, respectively) or a
 * relative file system path (if the superproject origin URL is a relative
 * file system path).
 *
 * When the output is a relative file system path, the path is either
 * relative to the submodule working tree, if up_path is specified, or to
 * the superproject working tree otherwise.
 *
 * NEEDSWORK: This works incorrectly on the domain and protocol part.
 * remote_url      url              outcome          expectation
 * http://a.com/b  ../c             http://a.com/c   as is
 * http://a.com/b/ ../c             http://a.com/c   same as previous line, but
 *                                                   ignore trailing slash in url
 * http://a.com/b  ../../c          http://c         error out
 * http://a.com/b  ../../../c       http:/c          error out
 * http://a.com/b  ../../../../c    http:c           error out
 * http://a.com/b  ../../../../../c    .:c           error out
 * http://a.com/b  http://d.org/e   http://d.org/e   as is
 * NEEDSWORK: Given how chop_last_dir() works, this function is broken
 * when a local part has a colon in its path component, too.
 */
char *relative_url(const char *remote_url, const char *url,
		   const char *up_path);

#endif
