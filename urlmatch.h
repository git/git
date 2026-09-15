#ifndef URL_MATCH_H
#define URL_MATCH_H

#include "string-list.h"
#include "config.h"

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
	size_t host_len;	/* length of host name;
				 * file urls may have host_len == 0 */
	size_t port_off;	/* offset into url to start of port number (0 => none) */
	size_t port_len;	/* if a portnum is present (port_off != 0), it has
				 * this length (excluding the leading ':') starting
				 * from port_off (always 0 for file urls) */
	size_t path_off;	/* offset into url to the start of the url path;
				 * this will always point to a '/' character
				 * after the url has been normalized */
	size_t path_len;	/* length of path portion excluding any trailing
				 * '?...' and '#...' portion; will always be >= 1 */
};

char *url_normalize(const char *, struct url_info *);
char *url_parse(const char *, struct url_info *);

/*
 * Like url_normalize(), but also allows '*' glob characters in the host
 * portion. Use this when normalizing URL patterns from user configuration.
 *
 * Note that '*' is a valid path character per RFC 3986 (as a sub-delim),
 * so glob patterns using '*' in the path are also accepted.
 *
 * Returns a newly allocated normalized string and fills out_info if
 * non-NULL, or NULL if the pattern is invalid.
 */
char *url_normalize_pattern(const char *url, struct url_info *out_info);

struct urlmatch_item {
	size_t hostmatch_len;
	size_t pathmatch_len;
	char user_matched;
};

struct urlmatch_config {
	struct string_list vars;
	struct url_info url;
	const char *section;
	const char *key;

	void *cb;
	config_fn_t collect_fn;
	config_fn_t cascade_fn;
	/*
	 * Compare the two matches, the one just discovered and the existing
	 * best match and return a negative value if the found item is to be
	 * rejected or a non-negative value if it is to be accepted.  If this
	 * field is set to NULL, use the default comparison technique, which
	 * checks to ses if found is better (according to the urlmatch
	 * specificity rules) than existing.
	 */
	int (*select_fn)(const struct urlmatch_item *found, const struct urlmatch_item *existing);
	/*
	 * An optional callback to allow e.g. for partial URLs; it shall
	 * return 1 or 0 depending whether `url` matches or not.
	 */
	int (*fallback_match_fn)(const char *url, void *cb);
};

#define URLMATCH_CONFIG_INIT { \
	.vars = STRING_LIST_INIT_DUP, \
}

int urlmatch_config_entry(const char *var, const char *value,
			  const struct config_context *ctx, void *cb);
void urlmatch_config_release(struct urlmatch_config *config);

#endif /* URL_MATCH_H */
