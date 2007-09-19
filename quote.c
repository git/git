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
#undef EMIT
#define EMIT(x) do { if (++len < n) *bp++ = (x); } while(0)

static inline int need_bs_quote(char c)
{
	return (c == '\'' || c == '!');
}

static size_t sq_quote_buf(char *dst, size_t n, const char *src)
{
	char c;
	char *bp = dst;
	size_t len = 0;

	EMIT('\'');
	while ((c = *src++)) {
		if (need_bs_quote(c)) {
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

void sq_quote_print(FILE *stream, const char *src)
{
	char c;

	fputc('\'', stream);
	while ((c = *src++)) {
		if (need_bs_quote(c)) {
			fputs("'\\", stream);
			fputc(c, stream);
			fputc('\'', stream);
		} else {
			fputc(c, stream);
		}
	}
	fputc('\'', stream);
}

char *sq_quote_argv(const char** argv, int count)
{
	char *buf, *to;
	int i;
	size_t len = 0;

	/* Count argv if needed. */
	if (count < 0) {
		for (count = 0; argv[count]; count++)
			; /* just counting */
	}

	/* Special case: no argv. */
	if (!count)
		return xcalloc(1,1);

	/* Get destination buffer length. */
	for (i = 0; i < count; i++)
		len += sq_quote_buf(NULL, 0, argv[i]) + 1;

	/* Alloc destination buffer. */
	to = buf = xmalloc(len + 1);

	/* Copy into destination buffer. */
	for (i = 0; i < count; ++i) {
		*to++ = ' ';
		to += sq_quote_buf(to, len, argv[i]);
	}

	return buf;
}

/*
 * Append a string to a string buffer, with or without shell quoting.
 * Return true if the buffer overflowed.
 */
int add_to_string(char **ptrp, int *sizep, const char *str, int quote)
{
	char *p = *ptrp;
	int size = *sizep;
	int oc;
	int err = 0;

	if (quote)
		oc = sq_quote_buf(p, size, str);
	else {
		oc = strlen(str);
		memcpy(p, str, (size <= oc) ? size - 1 : oc);
	}

	if (size <= oc) {
		err = 1;
		oc = size - 1;
	}

	*ptrp += oc;
	**ptrp = '\0';
	*sizep -= oc;
	return err;
}

char *sq_dequote(char *arg)
{
	char *dst = arg;
	char *src = arg;
	char c;

	if (*src != '\'')
		return NULL;
	for (;;) {
		c = *++src;
		if (!c)
			return NULL;
		if (c != '\'') {
			*dst++ = c;
			continue;
		}
		/* We stepped out of sq */
		switch (*++src) {
		case '\0':
			*dst = 0;
			return arg;
		case '\\':
			c = *++src;
			if (need_bs_quote(c) && *++src == '\'') {
				*dst++ = c;
				continue;
			}
		/* Fallthrough */
		default:
			return NULL;
		}
	}
}

/*
 * C-style name quoting.
 *
 * Does one of three things:
 *
 * (1) if outbuf and outfp are both NULL, inspect the input name and
 *     counts the number of bytes that are needed to hold c_style
 *     quoted version of name, counting the double quotes around
 *     it but not terminating NUL, and returns it.  However, if name
 *     does not need c_style quoting, it returns 0.
 *
 * (2) if outbuf is not NULL, it must point at a buffer large enough
 *     to hold the c_style quoted version of name, enclosing double
 *     quotes, and terminating NUL.  Fills outbuf with c_style quoted
 *     version of name enclosed in double-quote pair.  Return value
 *     is undefined.
 *
 * (3) if outfp is not NULL, outputs c_style quoted version of name,
 *     but not enclosed in double-quote pair.  Return value is undefined.
 */

static int quote_c_style_counted(const char *name, int namelen,
				 char *outbuf, FILE *outfp, int no_dq)
{
#undef EMIT
#define EMIT(c) \
	(outbuf ? (*outbuf++ = (c)) : outfp ? fputc(c, outfp) : (count++))

#define EMITQ() EMIT('\\')

	const char *sp;
	unsigned char ch;
	int count = 0, needquote = 0;

	if (!no_dq)
		EMIT('"');
	for (sp = name; sp < name + namelen; sp++) {
		ch = *sp;
		if (!ch)
			break;
		if ((ch < ' ') || (ch == '"') || (ch == '\\') ||
		    (quote_path_fully && (ch >= 0177))) {
			needquote = 1;
			switch (ch) {
			case '\a': EMITQ(); ch = 'a'; break;
			case '\b': EMITQ(); ch = 'b'; break;
			case '\f': EMITQ(); ch = 'f'; break;
			case '\n': EMITQ(); ch = 'n'; break;
			case '\r': EMITQ(); ch = 'r'; break;
			case '\t': EMITQ(); ch = 't'; break;
			case '\v': EMITQ(); ch = 'v'; break;

			case '\\': /* fallthru */
			case '"': EMITQ(); break;
			default:
				/* octal */
				EMITQ();
				EMIT(((ch >> 6) & 03) + '0');
				EMIT(((ch >> 3) & 07) + '0');
				ch = (ch & 07) + '0';
				break;
			}
		}
		EMIT(ch);
	}
	if (!no_dq)
		EMIT('"');
	if (outbuf)
		*outbuf = 0;

	return needquote ? count : 0;
}

int quote_c_style(const char *name, char *outbuf, FILE *outfp, int no_dq)
{
	int cnt = strlen(name);
	return quote_c_style_counted(name, cnt, outbuf, outfp, no_dq);
}

/*
 * C-style name unquoting.
 *
 * Quoted should point at the opening double quote.
 * + Returns 0 if it was able to unquote the string properly, and appends the
 *   result in the strbuf `sb'.
 * + Returns -1 in case of error, and doesn't touch the strbuf. Though note
 *   that this function will allocate memory in the strbuf, so calling
 *   strbuf_release is mandatory whichever result unquote_c_style returns.
 *
 * Updates endp pointer to point at one past the ending double quote if given.
 */
int unquote_c_style(struct strbuf *sb, const char *quoted, const char **endp)
{
	size_t oldlen = sb->len, len;
	int ch, ac;

	if (*quoted++ != '"')
		return -1;

	for (;;) {
		len = strcspn(quoted, "\"\\");
		strbuf_add(sb, quoted, len);
		quoted += len;

		switch (*quoted++) {
		  case '"':
			if (endp)
				*endp = quoted + 1;
			return 0;
		  case '\\':
			break;
		  default:
			goto error;
		}

		switch ((ch = *quoted++)) {
		case 'a': ch = '\a'; break;
		case 'b': ch = '\b'; break;
		case 'f': ch = '\f'; break;
		case 'n': ch = '\n'; break;
		case 'r': ch = '\r'; break;
		case 't': ch = '\t'; break;
		case 'v': ch = '\v'; break;

		case '\\': case '"':
			break; /* verbatim */

		/* octal values with first digit over 4 overflow */
		case '0': case '1': case '2': case '3':
					ac = ((ch - '0') << 6);
			if ((ch = *quoted++) < '0' || '7' < ch)
				goto error;
					ac |= ((ch - '0') << 3);
			if ((ch = *quoted++) < '0' || '7' < ch)
				goto error;
					ac |= (ch - '0');
					ch = ac;
					break;
				default:
			goto error;
			}
		strbuf_addch(sb, ch);
		}

  error:
	strbuf_setlen(sb, oldlen);
	return -1;
}

void write_name_quoted(const char *prefix, int prefix_len,
		       const char *name, int quote, FILE *out)
{
	int needquote;

	if (!quote) {
	no_quote:
		if (prefix_len)
			fprintf(out, "%.*s", prefix_len, prefix);
		fputs(name, out);
		return;
	}

	needquote = 0;
	if (prefix_len)
		needquote = quote_c_style_counted(prefix, prefix_len,
						  NULL, NULL, 0);
	if (!needquote)
		needquote = quote_c_style(name, NULL, NULL, 0);
	if (needquote) {
		fputc('"', out);
		if (prefix_len)
			quote_c_style_counted(prefix, prefix_len,
					      NULL, out, 1);
		quote_c_style(name, NULL, out, 1);
		fputc('"', out);
	}
	else
		goto no_quote;
}

/* quoting as a string literal for other languages */

void perl_quote_print(FILE *stream, const char *src)
{
	const char sq = '\'';
	const char bq = '\\';
	char c;

	fputc(sq, stream);
	while ((c = *src++)) {
		if (c == sq || c == bq)
			fputc(bq, stream);
		fputc(c, stream);
	}
	fputc(sq, stream);
}

void python_quote_print(FILE *stream, const char *src)
{
	const char sq = '\'';
	const char bq = '\\';
	const char nl = '\n';
	char c;

	fputc(sq, stream);
	while ((c = *src++)) {
		if (c == nl) {
			fputc(bq, stream);
			fputc('n', stream);
			continue;
		}
		if (c == sq || c == bq)
			fputc(bq, stream);
		fputc(c, stream);
	}
	fputc(sq, stream);
}

void tcl_quote_print(FILE *stream, const char *src)
{
	char c;

	fputc('"', stream);
	while ((c = *src++)) {
		switch (c) {
		case '[': case ']':
		case '{': case '}':
		case '$': case '\\': case '"':
			fputc('\\', stream);
		default:
			fputc(c, stream);
			break;
		case '\f':
			fputs("\\f", stream);
			break;
		case '\r':
			fputs("\\r", stream);
			break;
		case '\n':
			fputs("\\n", stream);
			break;
		case '\t':
			fputs("\\t", stream);
			break;
		case '\v':
			fputs("\\v", stream);
			break;
		}
	}
	fputc('"', stream);
}
