#include "cache.h"
#include "urlmatch.h"

#define URL_ALPHA "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
#define URL_DIGIT "0123456789"
#define URL_ALPHADIGIT URL_ALPHA URL_DIGIT
#define URL_SCHEME_CHARS URL_ALPHADIGIT "+.-"
#define URL_HOST_CHARS URL_ALPHADIGIT ".-[:]" /* IPv6 literals need [:] */
#define URL_UNSAFE_CHARS " <>\"%{}|\\^`" /* plus 0x00-0x1F,0x7F-0xFF */
#define URL_GEN_RESERVED ":/?#[]@"
#define URL_SUB_RESERVED "!$&'()*+,;="
#define URL_RESERVED URL_GEN_RESERVED URL_SUB_RESERVED /* only allowed delims */

static int append_normalized_escapes(struct strbuf *buf,
				     const char *from,
				     size_t from_len,
				     const char *esc_extra,
				     const char *esc_ok)
{
	/*
	 * Append to strbuf 'buf' characters from string 'from' with length
	 * 'from_len' while unescaping characters that do not need to be escaped
	 * and escaping characters that do.  The set of characters to escape
	 * (the complement of which is unescaped) starts out as the RFC 3986
	 * unsafe characters (0x00-0x1F,0x7F-0xFF," <>\"#%{}|\\^`").  If
	 * 'esc_extra' is not NULL, those additional characters will also always
	 * be escaped.  If 'esc_ok' is not NULL, those characters will be left
	 * escaped if found that way, but will not be unescaped otherwise (used
	 * for delimiters).  If a %-escape sequence is encountered that is not
	 * followed by 2 hexadecimal digits, the sequence is invalid and
	 * false (0) will be returned.  Otherwise true (1) will be returned for
	 * success.
	 *
	 * Note that all %-escape sequences will be normalized to UPPERCASE
	 * as indicated in RFC 3986.  Unless included in esc_extra or esc_ok
	 * alphanumerics and "-._~" will always be unescaped as per RFC 3986.
	 */

	while (from_len) {
		int ch = *from++;
		int was_esc = 0;

		from_len--;
		if (ch == '%') {
			if (from_len < 2 ||
			    !isxdigit(from[0]) ||
			    !isxdigit(from[1]))
				return 0;
			ch = hexval(*from++) << 4;
			ch |= hexval(*from++);
			from_len -= 2;
			was_esc = 1;
		}
		if ((unsigned char)ch <= 0x1F || (unsigned char)ch >= 0x7F ||
		    strchr(URL_UNSAFE_CHARS, ch) ||
		    (esc_extra && strchr(esc_extra, ch)) ||
		    (was_esc && strchr(esc_ok, ch)))
			strbuf_addf(buf, "%%%02X", (unsigned char)ch);
		else
			strbuf_addch(buf, ch);
	}

	return 1;
}

char *url_normalize(const char *url, struct url_info *out_info)
{
	/*
	 * Normalize NUL-terminated url using the following rules:
	 *
	 * 1. Case-insensitive parts of url will be converted to lower case
	 * 2. %-encoded characters that do not need to be will be unencoded
	 * 3. Characters that are not %-encoded and must be will be encoded
	 * 4. All %-encodings will be converted to upper case hexadecimal
	 * 5. Leading 0s are removed from port numbers
	 * 6. If the default port for the scheme is given it will be removed
	 * 7. A path part (including empty) not starting with '/' has one added
	 * 8. Any dot segments (. or ..) in the path are resolved and removed
	 * 9. IPv6 host literals are allowed (but not normalized or validated)
	 *
	 * The rules are based on information in RFC 3986.
	 *
	 * Please note this function requires a full URL including a scheme
	 * and host part (except for file: URLs which may have an empty host).
	 *
	 * The return value is a newly allocated string that must be freed
	 * or NULL if the url is not valid.
	 *
	 * If out_info is non-NULL, the url and err fields therein will always
	 * be set.  If a non-NULL value is returned, it will be stored in
	 * out_info->url as well, out_info->err will be set to NULL and the
	 * other fields of *out_info will also be filled in.  If a NULL value
	 * is returned, NULL will be stored in out_info->url and out_info->err
	 * will be set to a brief, translated, error message, but no other
	 * fields will be filled in.
	 *
	 * This is NOT a URL validation function.  Full URL validation is NOT
	 * performed.  Some invalid host names are passed through this function
	 * undetected.  However, most all other problems that make a URL invalid
	 * will be detected (including a missing host for non file: URLs).
	 */

	size_t url_len = strlen(url);
	struct strbuf norm;
	size_t spanned;
	size_t scheme_len, user_off=0, user_len=0, passwd_off=0, passwd_len=0;
	size_t host_off=0, host_len=0, port_len=0, path_off, path_len, result_len;
	const char *slash_ptr, *at_ptr, *colon_ptr, *path_start;
	char *result;

	/*
	 * Copy lowercased scheme and :// suffix, %-escapes are not allowed
	 * First character of scheme must be URL_ALPHA
	 */
	spanned = strspn(url, URL_SCHEME_CHARS);
	if (!spanned || !isalpha(url[0]) || spanned + 3 > url_len ||
	    url[spanned] != ':' || url[spanned+1] != '/' || url[spanned+2] != '/') {
		if (out_info) {
			out_info->url = NULL;
			out_info->err = _("invalid URL scheme name or missing '://' suffix");
		}
		return NULL; /* Bad scheme and/or missing "://" part */
	}
	strbuf_init(&norm, url_len);
	scheme_len = spanned;
	spanned += 3;
	url_len -= spanned;
	while (spanned--)
		strbuf_addch(&norm, tolower(*url++));


	/*
	 * Copy any username:password if present normalizing %-escapes
	 */
	at_ptr = strchr(url, '@');
	slash_ptr = url + strcspn(url, "/?#");
	if (at_ptr && at_ptr < slash_ptr) {
		user_off = norm.len;
		if (at_ptr > url) {
			if (!append_normalized_escapes(&norm, url, at_ptr - url,
						       "", URL_RESERVED)) {
				if (out_info) {
					out_info->url = NULL;
					out_info->err = _("invalid %XX escape sequence");
				}
				strbuf_release(&norm);
				return NULL;
			}
			colon_ptr = strchr(norm.buf + scheme_len + 3, ':');
			if (colon_ptr) {
				passwd_off = (colon_ptr + 1) - norm.buf;
				passwd_len = norm.len - passwd_off;
				user_len = (passwd_off - 1) - (scheme_len + 3);
			} else {
				user_len = norm.len - (scheme_len + 3);
			}
		}
		strbuf_addch(&norm, '@');
		url_len -= (++at_ptr - url);
		url = at_ptr;
	}


	/*
	 * Copy the host part excluding any port part, no %-escapes allowed
	 */
	if (!url_len || strchr(":/?#", *url)) {
		/* Missing host invalid for all URL schemes except file */
		if (strncmp(norm.buf, "file:", 5)) {
			if (out_info) {
				out_info->url = NULL;
				out_info->err = _("missing host and scheme is not 'file:'");
			}
			strbuf_release(&norm);
			return NULL;
		}
	} else {
		host_off = norm.len;
	}
	colon_ptr = slash_ptr - 1;
	while (colon_ptr > url && *colon_ptr != ':' && *colon_ptr != ']')
		colon_ptr--;
	if (*colon_ptr != ':') {
		colon_ptr = slash_ptr;
	} else if (!host_off && colon_ptr < slash_ptr && colon_ptr + 1 != slash_ptr) {
		/* file: URLs may not have a port number */
		if (out_info) {
			out_info->url = NULL;
			out_info->err = _("a 'file:' URL may not have a port number");
		}
		strbuf_release(&norm);
		return NULL;
	}
	spanned = strspn(url, URL_HOST_CHARS);
	if (spanned < colon_ptr - url) {
		/* Host name has invalid characters */
		if (out_info) {
			out_info->url = NULL;
			out_info->err = _("invalid characters in host name");
		}
		strbuf_release(&norm);
		return NULL;
	}
	while (url < colon_ptr) {
		strbuf_addch(&norm, tolower(*url++));
		url_len--;
	}


	/*
	 * Check the port part and copy if not the default (after removing any
	 * leading 0s); no %-escapes allowed
	 */
	if (colon_ptr < slash_ptr) {
		/* skip the ':' and leading 0s but not the last one if all 0s */
		url++;
		url += strspn(url, "0");
		if (url == slash_ptr && url[-1] == '0')
			url--;
		if (url == slash_ptr) {
			/* Skip ":" port with no number, it's same as default */
		} else if (slash_ptr - url == 2 &&
			   !strncmp(norm.buf, "http:", 5) &&
			   !strncmp(url, "80", 2)) {
			/* Skip http :80 as it's the default */
		} else if (slash_ptr - url == 3 &&
			   !strncmp(norm.buf, "https:", 6) &&
			   !strncmp(url, "443", 3)) {
			/* Skip https :443 as it's the default */
		} else {
			/*
			 * Port number must be all digits with leading 0s removed
			 * and since all the protocols we deal with have a 16-bit
			 * port number it must also be in the range 1..65535
			 * 0 is not allowed because that means "next available"
			 * on just about every system and therefore cannot be used
			 */
			unsigned long pnum = 0;
			spanned = strspn(url, URL_DIGIT);
			if (spanned < slash_ptr - url) {
				/* port number has invalid characters */
				if (out_info) {
					out_info->url = NULL;
					out_info->err = _("invalid port number");
				}
				strbuf_release(&norm);
				return NULL;
			}
			if (slash_ptr - url <= 5)
				pnum = strtoul(url, NULL, 10);
			if (pnum == 0 || pnum > 65535) {
				/* port number not in range 1..65535 */
				if (out_info) {
					out_info->url = NULL;
					out_info->err = _("invalid port number");
				}
				strbuf_release(&norm);
				return NULL;
			}
			strbuf_addch(&norm, ':');
			strbuf_add(&norm, url, slash_ptr - url);
			port_len = slash_ptr - url;
		}
		url_len -= slash_ptr - colon_ptr;
		url = slash_ptr;
	}
	if (host_off)
		host_len = norm.len - host_off;


	/*
	 * Now copy the path resolving any . and .. segments being careful not
	 * to corrupt the URL by unescaping any delimiters, but do add an
	 * initial '/' if it's missing and do normalize any %-escape sequences.
	 */
	path_off = norm.len;
	path_start = norm.buf + path_off;
	strbuf_addch(&norm, '/');
	if (*url == '/') {
		url++;
		url_len--;
	}
	for (;;) {
		const char *seg_start;
		size_t seg_start_off = norm.len;
		const char *next_slash = url + strcspn(url, "/?#");
		int skip_add_slash = 0;

		/*
		 * RFC 3689 indicates that any . or .. segments should be
		 * unescaped before being checked for.
		 */
		if (!append_normalized_escapes(&norm, url, next_slash - url, "",
					       URL_RESERVED)) {
			if (out_info) {
				out_info->url = NULL;
				out_info->err = _("invalid %XX escape sequence");
			}
			strbuf_release(&norm);
			return NULL;
		}

		seg_start = norm.buf + seg_start_off;
		if (!strcmp(seg_start, ".")) {
			/* ignore a . segment; be careful not to remove initial '/' */
			if (seg_start == path_start + 1) {
				strbuf_setlen(&norm, norm.len - 1);
				skip_add_slash = 1;
			} else {
				strbuf_setlen(&norm, norm.len - 2);
			}
		} else if (!strcmp(seg_start, "..")) {
			/*
			 * ignore a .. segment and remove the previous segment;
			 * be careful not to remove initial '/' from path
			 */
			const char *prev_slash = norm.buf + norm.len - 3;
			if (prev_slash == path_start) {
				/* invalid .. because no previous segment to remove */
				if (out_info) {
					out_info->url = NULL;
					out_info->err = _("invalid '..' path segment");
				}
				strbuf_release(&norm);
				return NULL;
			}
			while (*--prev_slash != '/') {}
			if (prev_slash == path_start) {
				strbuf_setlen(&norm, prev_slash - norm.buf + 1);
				skip_add_slash = 1;
			} else {
				strbuf_setlen(&norm, prev_slash - norm.buf);
			}
		}
		url_len -= next_slash - url;
		url = next_slash;
		/* if the next char is not '/' done with the path */
		if (*url != '/')
			break;
		url++;
		url_len--;
		if (!skip_add_slash)
			strbuf_addch(&norm, '/');
	}
	path_len = norm.len - path_off;


	/*
	 * Now simply copy the rest, if any, only normalizing %-escapes and
	 * being careful not to corrupt the URL by unescaping any delimiters.
	 */
	if (*url) {
		if (!append_normalized_escapes(&norm, url, url_len, "", URL_RESERVED)) {
			if (out_info) {
				out_info->url = NULL;
				out_info->err = _("invalid %XX escape sequence");
			}
			strbuf_release(&norm);
			return NULL;
		}
	}


	result = strbuf_detach(&norm, &result_len);
	if (out_info) {
		out_info->url = result;
		out_info->err = NULL;
		out_info->url_len = result_len;
		out_info->scheme_len = scheme_len;
		out_info->user_off = user_off;
		out_info->user_len = user_len;
		out_info->passwd_off = passwd_off;
		out_info->passwd_len = passwd_len;
		out_info->host_off = host_off;
		out_info->host_len = host_len;
		out_info->port_len = port_len;
		out_info->path_off = path_off;
		out_info->path_len = path_len;
	}
	return result;
}

static size_t url_match_prefix(const char *url,
			       const char *url_prefix,
			       size_t url_prefix_len)
{
	/*
	 * url_prefix matches url if url_prefix is an exact match for url or it
	 * is a prefix of url and the match ends on a path component boundary.
	 * Both url and url_prefix are considered to have an implicit '/' on the
	 * end for matching purposes if they do not already.
	 *
	 * url must be NUL terminated.  url_prefix_len is the length of
	 * url_prefix which need not be NUL terminated.
	 *
	 * The return value is the length of the match in characters (including
	 * the final '/' even if it's implicit) or 0 for no match.
	 *
	 * Passing NULL as url and/or url_prefix will always cause 0 to be
	 * returned without causing any faults.
	 */
	if (!url || !url_prefix)
		return 0;
	if (!url_prefix_len || (url_prefix_len == 1 && *url_prefix == '/'))
		return (!*url || *url == '/') ? 1 : 0;
	if (url_prefix[url_prefix_len - 1] == '/')
		url_prefix_len--;
	if (strncmp(url, url_prefix, url_prefix_len))
		return 0;
	if ((strlen(url) == url_prefix_len) || (url[url_prefix_len] == '/'))
		return url_prefix_len + 1;
	return 0;
}

static int match_urls(const struct url_info *url,
		      const struct url_info *url_prefix,
		      int *exactusermatch)
{
	/*
	 * url_prefix matches url if the scheme, host and port of url_prefix
	 * are the same as those of url and the path portion of url_prefix
	 * is the same as the path portion of url or it is a prefix that
	 * matches at a '/' boundary.  If url_prefix contains a user name,
	 * that must also exactly match the user name in url.
	 *
	 * If the user, host, port and path match in this fashion, the returned
	 * value is the length of the path match including any implicit
	 * final '/'.  For example, "http://me@example.com/path" is matched by
	 * "http://example.com" with a path length of 1.
	 *
	 * If there is a match and exactusermatch is not NULL, then
	 * *exactusermatch will be set to true if both url and url_prefix
	 * contained a user name or false if url_prefix did not have a
	 * user name.  If there is no match *exactusermatch is left untouched.
	 */
	int usermatched = 0;
	int pathmatchlen;

	if (!url || !url_prefix || !url->url || !url_prefix->url)
		return 0;

	/* check the scheme */
	if (url_prefix->scheme_len != url->scheme_len ||
	    strncmp(url->url, url_prefix->url, url->scheme_len))
		return 0; /* schemes do not match */

	/* check the user name if url_prefix has one */
	if (url_prefix->user_off) {
		if (!url->user_off || url->user_len != url_prefix->user_len ||
		    strncmp(url->url + url->user_off,
			    url_prefix->url + url_prefix->user_off,
			    url->user_len))
			return 0; /* url_prefix has a user but it's not a match */
		usermatched = 1;
	}

	/* check the host and port */
	if (url_prefix->host_len != url->host_len ||
	    strncmp(url->url + url->host_off,
		    url_prefix->url + url_prefix->host_off, url->host_len))
		return 0; /* host names and/or ports do not match */

	/* check the path */
	pathmatchlen = url_match_prefix(
		url->url + url->path_off,
		url_prefix->url + url_prefix->path_off,
		url_prefix->url_len - url_prefix->path_off);

	if (pathmatchlen && exactusermatch)
		*exactusermatch = usermatched;
	return pathmatchlen;
}

int urlmatch_config_entry(const char *var, const char *value, void *cb)
{
	struct string_list_item *item;
	struct urlmatch_config *collect = cb;
	struct urlmatch_item *matched;
	struct url_info *url = &collect->url;
	const char *key, *dot;
	struct strbuf synthkey = STRBUF_INIT;
	size_t matched_len = 0;
	int user_matched = 0;
	int retval;

	if (!skip_prefix(var, collect->section, &key) || *(key++) != '.') {
		if (collect->cascade_fn)
			return collect->cascade_fn(var, value, cb);
		return 0; /* not interested */
	}
	dot = strrchr(key, '.');
	if (dot) {
		char *config_url, *norm_url;
		struct url_info norm_info;

		config_url = xmemdupz(key, dot - key);
		norm_url = url_normalize(config_url, &norm_info);
		free(config_url);
		if (!norm_url)
			return 0;
		matched_len = match_urls(url, &norm_info, &user_matched);
		free(norm_url);
		if (!matched_len)
			return 0;
		key = dot + 1;
	}

	if (collect->key && strcmp(key, collect->key))
		return 0;

	item = string_list_insert(&collect->vars, key);
	if (!item->util) {
		matched = xcalloc(1, sizeof(*matched));
		item->util = matched;
	} else {
		matched = item->util;
		/*
		 * Is our match shorter?  Is our match the same
		 * length, and without user while the current
		 * candidate is with user?  Then we cannot use it.
		 */
		if (matched_len < matched->matched_len ||
		    ((matched_len == matched->matched_len) &&
		     (!user_matched && matched->user_matched)))
			return 0;
		/* Otherwise, replace it with this one. */
	}

	matched->matched_len = matched_len;
	matched->user_matched = user_matched;
	strbuf_addstr(&synthkey, collect->section);
	strbuf_addch(&synthkey, '.');
	strbuf_addstr(&synthkey, key);
	retval = collect->collect_fn(synthkey.buf, value, collect->cb);

	strbuf_release(&synthkey);
	return retval;
}
