#ifndef REMOTE_H
#define REMOTE_H

#include "parse-options.h"
#include "hashmap.h"

enum {
	REMOTE_CONFIG,
	REMOTE_REMOTES,
	REMOTE_BRANCHES
};

struct remote {
	struct hashmap_entry ent;  /* must be first */

	const char *name;
	int origin;

	const char *foreign_vcs;

	const char **url;
	int url_nr;
	int url_alloc;

	const char **pushurl;
	int pushurl_nr;
	int pushurl_alloc;

	const char **push_refspec;
	struct refspec *push;
	int push_refspec_nr;
	int push_refspec_alloc;

	const char **fetch_refspec;
	struct refspec *fetch;
	int fetch_refspec_nr;
	int fetch_refspec_alloc;

	/*
	 * -1 to never fetch tags
	 * 0 to auto-follow tags on heuristic (default)
	 * 1 to always auto-follow tags
	 * 2 to always fetch tags
	 */
	int fetch_tags;
	int skip_default_update;
	int mirror;
	int prune;

	const char *receivepack;
	const char *uploadpack;

	/*
	 * for curl remotes only
	 */
	char *http_proxy;
};

struct remote *remote_get(const char *name);
struct remote *pushremote_get(const char *name);
int remote_is_configured(const char *name);

typedef int each_remote_fn(struct remote *remote, void *priv);
int for_each_remote(each_remote_fn fn, void *priv);

int remote_has_url(struct remote *remote, const char *url);

struct refspec {
	unsigned force : 1;
	unsigned pattern : 1;
	unsigned matching : 1;
	unsigned exact_sha1 : 1;

	char *src;
	char *dst;
};

extern const struct refspec *tag_refspec;

struct ref {
	struct ref *next;
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	unsigned char old_sha1_expect[20]; /* used by expect-old */
	char *symref;
	unsigned int
		force:1,
		forced_update:1,
		expect_old_sha1:1,
		expect_old_no_trackback:1,
		deletion:1,
		matched:1;

	/*
	 * Order is important here, as we write to FETCH_HEAD
	 * in numeric order. And the default NOT_FOR_MERGE
	 * should be 0, so that xcalloc'd structures get it
	 * by default.
	 */
	enum {
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
		REF_STATUS_UPTODATE,
		REF_STATUS_REMOTE_REJECT,
		REF_STATUS_EXPECTING_REPORT,
		REF_STATUS_ATOMIC_PUSH_FAILED
	} status;
	char *remote_status;
	struct ref *peer_ref; /* when renaming */
	char name[FLEX_ARRAY]; /* more */
};

#define REF_NORMAL	(1u << 0)
#define REF_HEADS	(1u << 1)
#define REF_TAGS	(1u << 2)

extern struct ref *find_ref_by_name(const struct ref *list, const char *name);

struct ref *alloc_ref(const char *name);
struct ref *copy_ref(const struct ref *ref);
struct ref *copy_ref_list(const struct ref *ref);
void sort_ref_list(struct ref **, int (*cmp)(const void *, const void *));
extern int count_refspec_match(const char *, struct ref *refs, struct ref **matched_ref);
int ref_compare_name(const void *, const void *);

int check_ref_type(const struct ref *ref, int flags);

/*
 * Frees the entire list and peers of elements.
 */
void free_refs(struct ref *ref);

struct sha1_array;
extern struct ref **get_remote_heads(int in, char *src_buf, size_t src_len,
				     struct ref **list, unsigned int flags,
				     struct sha1_array *extra_have,
				     struct sha1_array *shallow);

int resolve_remote_symref(struct ref *ref, struct ref *list);
int ref_newer(const unsigned char *new_sha1, const unsigned char *old_sha1);

/*
 * Remove and free all but the first of any entries in the input list
 * that map the same remote reference to the same local reference.  If
 * there are two entries that map different remote references to the
 * same local reference, emit an error message and die.  Return a
 * pointer to the head of the resulting list.
 */
struct ref *ref_remove_duplicates(struct ref *ref_map);

int valid_fetch_refspec(const char *refspec);
struct refspec *parse_fetch_refspec(int nr_refspec, const char **refspec);

void free_refspec(int nr_refspec, struct refspec *refspec);

extern int query_refspecs(struct refspec *specs, int nr, struct refspec *query);
char *apply_refspecs(struct refspec *refspecs, int nr_refspec,
		     const char *name);

int check_push_refs(struct ref *src, int nr_refspec, const char **refspec);
int match_push_refs(struct ref *src, struct ref **dst,
		    int nr_refspec, const char **refspec, int all);
void set_ref_status_for_push(struct ref *remote_refs, int send_mirror,
	int force_update);

/*
 * Given a list of the remote refs and the specification of things to
 * fetch, makes a (separate) list of the refs to fetch and the local
 * refs to store into.
 *
 * *tail is the pointer to the tail pointer of the list of results
 * beforehand, and will be set to the tail pointer of the list of
 * results afterward.
 *
 * missing_ok is usually false, but when we are adding branch.$name.merge
 * it is Ok if the branch is not at the remote anymore.
 */
int get_fetch_map(const struct ref *remote_refs, const struct refspec *refspec,
		  struct ref ***tail, int missing_ok);

struct ref *get_remote_ref(const struct ref *remote_refs, const char *name);

/*
 * For the given remote, reads the refspec's src and sets the other fields.
 */
int remote_find_tracking(struct remote *remote, struct refspec *refspec);

struct branch {
	const char *name;
	const char *refname;

	const char *remote_name;
	const char *pushremote_name;

	const char **merge_name;
	struct refspec **merge;
	int merge_nr;
	int merge_alloc;

	const char *push_tracking_ref;
};

struct branch *branch_get(const char *name);
const char *remote_for_branch(struct branch *branch, int *explicit);
const char *pushremote_for_branch(struct branch *branch, int *explicit);

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

/* Reporting of tracking info */
int stat_tracking_info(struct branch *branch, int *num_ours, int *num_theirs,
		       const char **upstream_name);
int format_tracking_info(struct branch *branch, struct strbuf *sb);

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
struct ref *get_stale_heads(struct refspec *refs, int ref_count, struct ref *fetch_map);

/*
 * Compare-and-swap
 */
#define CAS_OPT_NAME "force-with-lease"

struct push_cas_option {
	unsigned use_tracking_for_rest:1;
	struct push_cas {
		unsigned char expect[20];
		unsigned use_tracking:1;
		char *refname;
	} *entry;
	int nr;
	int alloc;
};

extern int parseopt_push_cas_option(const struct option *, const char *arg, int unset);
extern int parse_push_cas_option(struct push_cas_option *, const char *arg, int unset);

extern int is_empty_cas(const struct push_cas_option *);
void apply_push_cas(struct push_cas_option *, struct remote *, struct ref *);

#endif
