#include "cache.h"
#include "url.h"

int is_urlschemechar(int first_flag, int ch)
{
	/*
	 * The set of valid URL schemes, as per STD66 (RFC3986) is
	 * '[A-Za-z][A-Za-z0-9+.-]*'. But use sightly looser check
	 * of '[A-Za-z0-9][A-Za-z0-9+.-]*' because earlier version
	 * of check used '[A-Za-z0-9]+' so not to break any remote
	 * helpers.
	 */
	int alphanumeric, special;
	alphanumeric = ch > 0 && isalnum(ch);
	special = ch == '+' || ch == '-' || ch == '.';
	return alphanumeric || (!first_flag && special);
}

int is_url(const char *url)
{
	/* Is "scheme" part reasonable? */
	if (!url || !is_urlschemechar(1, *url++))
		return 0;
	while (*url && *url != ':') {
		if (!is_urlschemechar(0, *url++))
			return 0;
	}
	/* We've seen "scheme"; we want colon-slash-slash */
	return (url[0] == ':' && url[1] == '/' && url[2] == '/');
}

static char *url_decode_internal(const char **query, int len,
				 const char *stop_at, struct strbuf *out,
				 int decode_plus)
{
	const char *q = *query;

	while (len) {
		unsigned char c = *q;

		if (!c)
			break;
		if (stop_at && strchr(stop_at, c)) {
			q++;
			len--;
			break;
		}

		if (c == '%') {
			int val = hex2chr(q + 1);
			if (0 <= val) {
				strbuf_addch(out, val);
				q += 3;
				len -= 3;
				continue;
			}
		}

		if (decode_plus && c == '+')
			strbuf_addch(out, ' ');
		else
			strbuf_addch(out, c);
		q++;
		len--;
	}
	*query = q;
	return strbuf_detach(out, NULL);
}

char *url_decode(const char *url)
{
	return url_decode_mem(url, strlen(url));
}

char *url_decode_mem(const char *url, int len)
{
	struct strbuf out = STRBUF_INIT;
	const char *colon = memchr(url, ':', len);

	/* Skip protocol part if present */
	if (colon && url < colon) {
		strbuf_add(&out, url, colon - url);
		len -= colon - url;
		url = colon;
	}
	return url_decode_internal(&url, len, NULL, &out, 0);
}

char *url_decode_parameter_name(const char **query)
{
	struct strbuf out = STRBUF_INIT;
	return url_decode_internal(query, -1, "&=", &out, 1);
}

char *url_decode_parameter_value(const char **query)
{
	struct strbuf out = STRBUF_INIT;
	return url_decode_internal(query, -1, "&", &out, 1);
}

void end_url_with_slash(struct strbuf *buf, const char *url)
{
	strbuf_addstr(buf, url);
	strbuf_complete(buf, '/');
}

void str_end_url_with_slash(const char *url, char **dest) {
	struct strbuf buf = STRBUF_INIT;
	end_url_with_slash(&buf, url);
	free(*dest);
	*dest = strbuf_detach(&buf, NULL);
}
