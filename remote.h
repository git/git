#ifndef REMOTE_H
#define REMOTE_H

enum {
	REMOTE_CONFIG,
	REMOTE_REMOTES,
	REMOTE_BRANCHES
};

struct remote {
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

	const char *receivepack;
	const char *uploadpack;

	/*
	 * for curl remotes only
	 */
	char *http_proxy;
};

struct remote *remote_get(const char *name);
int remote_is_configured(const char *name);

typedef int each_remote_fn(struct remote *remote, void *priv);
int for_each_remote(each_remote_fn fn, void *priv);

int remote_has_url(struct remote *remote, const char *url);

struct refspec {
	unsigned force : 1;
	unsigned pattern : 1;
	unsigned matching : 1;

	char *src;
	char *dst;
};

extern const struct refspec *tag_refspec;

struct ref *alloc_ref(const char *name);
struct ref *copy_ref(const struct ref *ref);
struct ref *copy_ref_list(const struct ref *ref);

int check_ref_type(const struct ref *ref, int flags);

/*
 * Frees the entire list and peers of elements.
 */
void free_refs(struct ref *ref);

int resolve_remote_symref(struct ref *ref, struct ref *list);
int ref_newer(const unsigned char *new_sha1, const unsigned char *old_sha1);

/*
 * Removes and frees any duplicate refs in the map.
 */
void ref_remove_duplicates(struct ref *ref_map);

int valid_fetch_refspec(const char *refspec);
struct refspec *parse_fetch_refspec(int nr_refspec, const char **refspec);

void free_refspec(int nr_refspec, struct refspec *refspec);

char *apply_refspecs(struct refspec *refspecs, int nr_refspec,
		     const char *name);

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
	struct remote *remote;

	const char **merge_name;
	struct refspec **merge;
	int merge_nr;
	int merge_alloc;
};

struct branch *branch_get(const char *name);

int branch_has_merge_config(struct branch *branch);
int branch_merge_matches(struct branch *, int n, const char *);

/* Flags to match_refs. */
enum match_refs_flags {
	MATCH_REFS_NONE		= 0,
	MATCH_REFS_ALL 		= (1 << 0),
	MATCH_REFS_MIRROR	= (1 << 1),
	MATCH_REFS_PRUNE	= (1 << 2)
};

/* Reporting of tracking info */
int stat_tracking_info(struct branch *branch, int *num_ours, int *num_theirs);
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

#endif
