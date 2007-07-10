#ifndef REMOTE_H
#define REMOTE_H

struct remote {
	const char *name;

	const char **uri;
	int uri_nr;

	const char **push_refspec;
	struct refspec *push;
	int push_refspec_nr;

	const char **fetch_refspec;
	struct refspec *fetch;
	int fetch_refspec_nr;

	const char *receivepack;
};

struct remote *remote_get(const char *name);

typedef int each_remote_fn(struct remote *remote, void *priv);
int for_each_remote(each_remote_fn fn, void *priv);

int remote_has_uri(struct remote *remote, const char *uri);

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

int match_refs(struct ref *src, struct ref *dst, struct ref ***dst_tail,
	       int nr_refspec, char **refspec, int all);

/*
 * For the given remote, reads the refspec's src and sets the other fields.
 */
int remote_find_tracking(struct remote *remote, struct refspec *refspec);

#endif
