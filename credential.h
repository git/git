#ifndef CREDENTIAL_H
#define CREDENTIAL_H

#include "string-list.h"

struct credential {
	struct string_list helpers;
	unsigned approved:1;

	char *username;
	char *password;
	char *protocol;
	char *host;
	char *path;
};

#define CREDENTIAL_INIT { STRING_LIST_INIT_DUP }

void credential_init(struct credential *);
void credential_clear(struct credential *);

void credential_fill(struct credential *);
void credential_approve(struct credential *);
void credential_reject(struct credential *);

int credential_read(struct credential *, FILE *);

#endif /* CREDENTIAL_H */
