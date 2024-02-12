#ifndef CREDENTIAL_H
#define CREDENTIAL_H

#include "string-list.h"
#include "strvec.h"

/**
 * The credentials API provides an abstracted way of gathering username and
 * password credentials from the user.
 *
 * Typical setup
 * -------------
 *
 * ------------
 * +-----------------------+
 * | Git code (C)          |--- to server requiring --->
 * |                       |        authentication
 * |.......................|
 * | C credential API      |--- prompt ---> User
 * +-----------------------+
 * 	^      |
 * 	| pipe |
 * 	|      v
 * +-----------------------+
 * | Git credential helper |
 * +-----------------------+
 * ------------
 *
 * The Git code (typically a remote-helper) will call the C API to obtain
 * credential data like a login/password pair (credential_fill). The
 * API will itself call a remote helper (e.g. "git credential-cache" or
 * "git credential-store") that may retrieve credential data from a
 * store. If the credential helper cannot find the information, the C API
 * will prompt the user. Then, the caller of the API takes care of
 * contacting the server, and does the actual authentication.
 *
 * C API
 * -----
 *
 * The credential C API is meant to be called by Git code which needs to
 * acquire or store a credential. It is centered around an object
 * representing a single credential and provides three basic operations:
 * fill (acquire credentials by calling helpers and/or prompting the user),
 * approve (mark a credential as successfully used so that it can be stored
 * for later use), and reject (mark a credential as unsuccessful so that it
 * can be erased from any persistent storage).
 *
 * Example
 * ~~~~~~~
 *
 * The example below shows how the functions of the credential API could be
 * used to login to a fictitious "foo" service on a remote host:
 *
 * -----------------------------------------------------------------------
 * int foo_login(struct foo_connection *f)
 * {
 * 	int status;
 * 	// Create a credential with some context; we don't yet know the
 * 	// username or password.
 *
 * struct credential c = CREDENTIAL_INIT;
 * c.protocol = xstrdup("foo");
 * c.host = xstrdup(f->hostname);
 *
 * // Fill in the username and password fields by contacting
 * // helpers and/or asking the user. The function will die if it
 * // fails.
 * credential_fill(&c);
 *
 * // Otherwise, we have a username and password. Try to use it.
 *
 * status = send_foo_login(f, c.username, c.password);
 * switch (status) {
 * case FOO_OK:
 * // It worked. Store the credential for later use.
 * credential_accept(&c);
 * break;
 * case FOO_BAD_LOGIN:
 * // Erase the credential from storage so we don't try it again.
 * credential_reject(&c);
 * break;
 * default:
 * // Some other error occurred. We don't know if the
 * // credential is good or bad, so report nothing to the
 * // credential subsystem.
 * }
 *
 * // Free any associated resources.
 * credential_clear(&c);
 *
 * return status;
 * }
 * -----------------------------------------------------------------------
 */


/**
 * This struct represents a single username/password combination
 * along with any associated context. All string fields should be
 * heap-allocated (or NULL if they are not known or not applicable).
 * The meaning of the individual context fields is the same as
 * their counterparts in the helper protocol.
 *
 * This struct should always be initialized with `CREDENTIAL_INIT` or
 * `credential_init`.
 */
struct credential {

	/**
	 * A `string_list` of helpers. Each string specifies an external
	 * helper which will be run, in order, to either acquire or store
	 * credentials. This list is filled-in by the API functions
	 * according to the corresponding configuration variables before
	 * consulting helpers, so there usually is no need for a caller to
	 * modify the helpers field at all.
	 */
	struct string_list helpers;

	/**
	 * A `strvec` of WWW-Authenticate header values. Each string
	 * is the value of a WWW-Authenticate header in an HTTP response,
	 * in the order they were received in the response.
	 */
	struct strvec wwwauth_headers;

	/**
	 * Internal use only. Keeps track of if we previously matched against a
	 * WWW-Authenticate header line in order to re-fold future continuation
	 * lines into one value.
	 */
	unsigned header_is_last_match:1;

	unsigned approved:1,
		 configured:1,
		 quit:1,
		 use_http_path:1,
		 username_from_proto:1;

	char *username;
	char *password;
	char *protocol;
	char *host;
	char *path;
	char *oauth_refresh_token;
	timestamp_t password_expiry_utc;
};

#define CREDENTIAL_INIT { \
	.helpers = STRING_LIST_INIT_DUP, \
	.password_expiry_utc = TIME_MAX, \
	.wwwauth_headers = STRVEC_INIT, \
}

/* Initialize a credential structure, setting all fields to empty. */
void credential_init(struct credential *);

/**
 * Free any resources associated with the credential structure, returning
 * it to a pristine initialized state.
 */
void credential_clear(struct credential *);

/**
 * Instruct the credential subsystem to fill the username and
 * password fields of the passed credential struct by first
 * consulting helpers, then asking the user. After this function
 * returns, the username and password fields of the credential are
 * guaranteed to be non-NULL. If an error occurs, the function will
 * die().
 */
void credential_fill(struct credential *);

/**
 * Inform the credential subsystem that the provided credentials
 * were successfully used for authentication.  This will cause the
 * credential subsystem to notify any helpers of the approval, so
 * that they may store the result to be used again.  Any errors
 * from helpers are ignored.
 */
void credential_approve(struct credential *);

/**
 * Inform the credential subsystem that the provided credentials
 * have been rejected. This will cause the credential subsystem to
 * notify any helpers of the rejection (which allows them, for
 * example, to purge the invalid credentials from storage). It
 * will also free() the username and password fields of the
 * credential and set them to NULL (readying the credential for
 * another call to `credential_fill`). Any errors from helpers are
 * ignored.
 */
void credential_reject(struct credential *);

int credential_read(struct credential *, FILE *);
void credential_write(const struct credential *, FILE *);

/*
 * Parse a url into a credential struct, replacing any existing contents.
 *
 * If the url can't be parsed (e.g., a missing "proto://" component), the
 * resulting credential will be empty and the function will return an
 * error (even in the "gently" form).
 *
 * If we encounter a component which cannot be represented as a credential
 * value (e.g., because it contains a newline), the "gently" form will return
 * an error but leave the broken state in the credential object for further
 * examination.  The non-gentle form will issue a warning to stderr and return
 * an empty credential.
 */
void credential_from_url(struct credential *, const char *url);
int credential_from_url_gently(struct credential *, const char *url, int quiet);

int credential_match(const struct credential *want,
		     const struct credential *have, int match_password);

#endif /* CREDENTIAL_H */
