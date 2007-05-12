#ifndef REMOTE_H
#define REMOTE_H

struct remote {
	const char *name;

	const char **uri;
	int uri_nr;

	const char **push_refspec;
	struct refspec *push;
	int push_refspec_nr;

	const char *receivepack;
};

struct remote *remote_get(const char *name);

struct refspec {
	unsigned force : 1;
	unsigned pattern : 1;

	const char *src;
	char *dst;
};

int match_refs(struct ref *src, struct ref *dst, struct ref ***dst_tail,
	       int nr_refspec, char **refspec, int all);

#endif
