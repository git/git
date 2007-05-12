#ifndef REMOTE_H
#define REMOTE_H

struct remote {
	const char *name;

	const char **uri;
	int uri_nr;

	const char **push_refspec;
	int push_refspec_nr;

	const char *receivepack;
};

struct remote *remote_get(const char *name);

#endif
