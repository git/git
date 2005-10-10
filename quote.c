#include "cache.h"
#include "quote.h"

/* Help to copy the thing properly quoted for the shell safety.
 * any single quote is replaced with '\'', any exclamation point
 * is replaced with '\!', and the whole thing is enclosed in a
 *
 * E.g.
 *  original     sq_quote     result
 *  name     ==> name      ==> 'name'
 *  a b      ==> a b       ==> 'a b'
 *  a'b      ==> a'\''b    ==> 'a'\''b'
 *  a!b      ==> a'\!'b    ==> 'a'\!'b'
 */
#define EMIT(x) ( (++len < n) && (*bp++ = (x)) )

size_t sq_quote_buf(char *dst, size_t n, const char *src)
{
	char c;
	char *bp = dst;
	size_t len = 0;

	EMIT('\'');
	while ((c = *src++)) {
		if (c == '\'' || c == '!') {
			EMIT('\'');
			EMIT('\\');
			EMIT(c);
			EMIT('\'');
		} else {
			EMIT(c);
		}
	}
	EMIT('\'');

	if ( n )
		*bp = 0;

	return len;
}

char *sq_quote(const char *src)
{
	char *buf;
	size_t cnt;

	cnt = sq_quote_buf(NULL, 0, src) + 1;
	buf = xmalloc(cnt);
	sq_quote_buf(buf, cnt, src);

	return buf;
}

