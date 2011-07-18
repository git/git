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
	const char *url2, *first_slash;

	if (!url)
		return 0;
	url2 = url;
	first_slash = strchr(url, '/');

	/* Input with no slash at all or slash first can't be URL. */
	if (!first_slash || first_slash == url)
		return 0;
	/* Character before must be : and next must be /. */
	if (first_slash[-1] != ':' || first_slash[1] != '/')
		return 0;
	/* There must be something before the :// */
	if (first_slash == url + 1)
		return 0;
	/*
	 * Check all characters up to first slash - 1. Only alphanum
	 * is allowed.
	 */
	url2 = url;
	while (url2 < first_slash - 1) {
		if (!is_urlschemechar(url2 == url, (unsigned char)*url2))
			return 0;
		url2++;
	}

	/* Valid enough. */
	return 1;
}

static int url_decode_char(const char *q)
{
	int i;
	unsigned char val = 0;
	for (i = 0; i < 2; i++) {
		unsigned char c = *q++;
		val <<= 4;
		if (c >= '0' && c <= '9')
			val += c - '0';
		else if (c >= 'a' && c <= 'f')
			val += c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			val += c - 'A' + 10;
		else
			return -1;
	}
	return val;
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
			int val = url_decode_char(q + 1);
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
	if (buf->len && buf->buf[buf->len - 1] != '/')
		strbuf_addstr(buf, "/");
}

void str_end_url_with_slash(const char *url, char **dest) {
	struct strbuf buf = STRBUF_INIT;
	end_url_with_slash(&buf, url);
	free(*dest);
	*dest = strbuf_detach(&buf, NULL);
}
