#ifndef URL_MATCH_H
#include "string-list.h"

struct url_info {
	/* normalized url on success, must be freed, otherwise NULL */
	char *url;
	/* if !url, a brief reason for the failure, otherwise NULL */
	const char *err;

	/* the rest of the fields are only set if url != NULL */

	size_t url_len;		/* total length of url (which is now normalized) */
	size_t scheme_len;	/* length of scheme name (excluding final :) */
	size_t user_off;	/* offset into url to start of user name (0 => none) */
	size_t user_len;	/* length of user name; if user_off != 0 but
				   user_len == 0, an empty user name was given */
	size_t passwd_off;	/* offset into url to start of passwd (0 => none) */
	size_t passwd_len;	/* length of passwd; if passwd_off != 0 but
				   passwd_len == 0, an empty passwd was given */
	size_t host_off;	/* offset into url to start of host name (0 => none) */
	size_t host_len;	/* length of host name; this INCLUDES any ':portnum';
				 * file urls may have host_len == 0 */
	size_t port_len;	/* if a portnum is present (port_len != 0), it has
				 * this length (excluding the leading ':') at the
				 * end of the host name (always 0 for file urls) */
	size_t path_off;	/* offset into url to the start of the url path;
				 * this will always point to a '/' character
				 * after the url has been normalized */
	size_t path_len;	/* length of path portion excluding any trailing
				 * '?...' and '#...' portion; will always be >= 1 */
};

extern char *url_normalize(const char *, struct url_info *);

struct urlmatch_item {
	size_t matched_len;
	char user_matched;
};

struct urlmatch_config {
	struct string_list vars;
	struct url_info url;
	const char *section;
	const char *key;

	void *cb;
	int (*collect_fn)(const char *var, const char *value, void *cb);
	int (*cascade_fn)(const char *var, const char *value, void *cb);
};

extern int urlmatch_config_entry(const char *var, const char *value, void *cb);

#endif /* URL_MATCH_H */
