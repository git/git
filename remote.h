#ifndef REMOTE_H
#define REMOTE_H

struct remote {
	const char *name;

	const char **url;
	int url_nr;

	const char **push_refspec;
	struct refspec *push;
	int push_refspec_nr;

	const char **fetch_refspec;
	struct refspec *fetch;
	int fetch_refspec_nr;

	/*
	 * -1 to never fetch tags
	 * 0 to auto-follow tags on heuristic (default)
	 * 1 to always auto-follow tags
	 * 2 to always fetch tags
	 */
	int fetch_tags;

	const char *receivepack;
	const char *uploadpack;
};

struct remote *remote_get(const char *name);

typedef int each_remote_fn(struct remote *remote, void *priv);
int for_each_remote(each_remote_fn fn, void *priv);

int remote_has_url(struct remote *remote, const char *url);

struct refspec {
	unsigned force : 1;
	unsigned pattern : 1;

	char *src;
	char *dst;
};

struct ref *alloc_ref(unsigned namelen);

/*
 * Frees the entire list and peers of elements.
 */
void free_refs(struct ref *ref);

/*
 * Removes and frees any duplicate refs in the map.
 */
void ref_remove_duplicates(struct ref *ref_map);

struct refspec *parse_ref_spec(int nr_refspec, const char **refspec);

int match_refs(struct ref *src, struct ref *dst, struct ref ***dst_tail,
	       int nr_refspec, char **refspec, int all);

/*
 * Given a list of the remote refs and the specification of things to
 * fetch, makes a (separate) list of the refs to fetch and the local
 * refs to store into.
 *
 * *tail is the pointer to the tail pointer of the list of results
 * beforehand, and will be set to the tail pointer of the list of
 * results afterward.
 */
int get_fetch_map(struct ref *remote_refs, const struct refspec *refspec,
		  struct ref ***tail);

struct ref *get_remote_ref(struct ref *remote_refs, const char *name);

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
};

struct branch *branch_get(const char *name);

int branch_has_merge_config(struct branch *branch);
int branch_merge_matches(struct branch *, int n, const char *);

#endif
