#ifndef CREDENTIAL_H
#define CREDENTIAL_H

#include "string-list.h"

struct credential {
	struct string_list helpers;
	unsigned approved:1,
		 configured:1,
		 quit:1,
		 use_http_path:1;

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
void credential_write(const struct credential *, FILE *);

/*
 * Parse a url into a credential struct, replacing any existing contents.
 *
 * Ifthe url can't be parsed (e.g., a missing "proto://" component), the
 * resulting credential will be empty but we'll still return success from the
 * "gently" form.
 *
 * If we encounter a component which cannot be represented as a credential
 * value (e.g., because it contains a newline), the "gently" form will return
 * an error but leave the broken state in the credential object for further
 * examination.  The non-gentle form will issue a warning to stderr and return
 * an empty credential.
 */
void credential_from_url(struct credential *, const char *url);
int credential_from_url_gently(struct credential *, const char *url, int quiet);

int credential_match(const struct credential *have,
		     const struct credential *want);

#endif /* CREDENTIAL_H */
