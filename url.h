#ifndef URL_H
#define URL_H

struct strbuf;

int is_url(const char *url);
int is_urlschemechar(int first_flag, int ch);
char *url_decode(const char *url);
char *url_decode_mem(const char *url, int len);

/*
 * Similar to the url_decode_{,mem} methods above, but doesn't assume there
 * is a scheme followed by a : at the start of the string. Instead, %-sequences
 * before any : are also parsed.
 */
char *url_percent_decode(const char *encoded);

char *url_decode_parameter_name(const char **query);
char *url_decode_parameter_value(const char **query);

void end_url_with_slash(struct strbuf *buf, const char *url);
void str_end_url_with_slash(const char *url, char **dest);

int url_is_local_not_ssh(const char *url);

enum url_scheme {
	URL_SCHEME_UNKNOWN = 0,
	URL_SCHEME_LOCAL,
	URL_SCHEME_FILE,
	URL_SCHEME_SSH,
	URL_SCHEME_GIT,
};

/*
 * Identify the URL scheme by name. Returns URL_SCHEME_UNKNOWN
 * if the name does not match any scheme that Git knows about.
 */
enum url_scheme url_get_scheme(const char *name);

/*
 * The set of unreserved characters as per STD66 (RFC3986) is
 * '[A-Za-z0-9-._~]'. These characters are safe to appear in URI
 * components without percent-encoding.
 */
int is_rfc3986_unreserved(char ch);

/*
 * This is a variant of is_rfc3986_unreserved() that treats uppercase
 * letters as "reserved". This forces them to be percent-encoded, allowing
 * 'Foo' (%46oo) and 'foo' (foo) to be distinct on case-folding filesystems.
 */
int is_casefolding_rfc3986_unreserved(char c);

#endif /* URL_H */
