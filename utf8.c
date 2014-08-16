#include "git-compat-util.h"
#include "strbuf.h"
#include "utf8.h"

/* This code is originally from http://www.cl.cam.ac.uk/~mgk25/ucs/ */

struct interval {
	ucs_char_t first;
	ucs_char_t last;
};

size_t display_mode_esc_sequence_len(const char *s)
{
	const char *p = s;
	if (*p++ != '\033')
		return 0;
	if (*p++ != '[')
		return 0;
	while (isdigit(*p) || *p == ';')
		p++;
	if (*p++ != 'm')
		return 0;
	return p - s;
}

/* auxiliary function for binary search in interval table */
static int bisearch(ucs_char_t ucs, const struct interval *table, int max)
{
	int min = 0;
	int mid;

	if (ucs < table[0].first || ucs > table[max].last)
		return 0;
	while (max >= min) {
		mid = (min + max) / 2;
		if (ucs > table[mid].last)
			min = mid + 1;
		else if (ucs < table[mid].first)
			max = mid - 1;
		else
			return 1;
	}

	return 0;
}

/* The following two functions define the column width of an ISO 10646
 * character as follows:
 *
 *    - The null character (U+0000) has a column width of 0.
 *
 *    - Other C0/C1 control characters and DEL will lead to a return
 *      value of -1.
 *
 *    - Non-spacing and enclosing combining characters (general
 *      category code Mn or Me in the Unicode database) have a
 *      column width of 0.
 *
 *    - SOFT HYPHEN (U+00AD) has a column width of 1.
 *
 *    - Other format characters (general category code Cf in the Unicode
 *      database) and ZERO WIDTH SPACE (U+200B) have a column width of 0.
 *
 *    - Hangul Jamo medial vowels and final consonants (U+1160-U+11FF)
 *      have a column width of 0.
 *
 *    - Spacing characters in the East Asian Wide (W) or East Asian
 *      Full-width (F) category as defined in Unicode Technical
 *      Report #11 have a column width of 2.
 *
 *    - All remaining characters (including all printable
 *      ISO 8859-1 and WGL4 characters, Unicode control characters,
 *      etc.) have a column width of 1.
 *
 * This implementation assumes that ucs_char_t characters are encoded
 * in ISO 10646.
 */

static int git_wcwidth(ucs_char_t ch)
{
	/*
	 * Sorted list of non-overlapping intervals of non-spacing characters,
	 */
#include "unicode_width.h"

	/* test for 8-bit control characters */
	if (ch == 0)
		return 0;
	if (ch < 32 || (ch >= 0x7f && ch < 0xa0))
		return -1;

	/* binary search in table of non-spacing characters */
	if (bisearch(ch, zero_width, sizeof(zero_width)
				/ sizeof(struct interval) - 1))
		return 0;

	/* binary search in table of double width characters */
	if (bisearch(ch, double_width, sizeof(double_width)
				/ sizeof(struct interval) - 1))
		return 2;

	return 1;
}

/*
 * Pick one ucs character starting from the location *start points at,
 * and return it, while updating the *start pointer to point at the
 * end of that character.  When remainder_p is not NULL, the location
 * holds the number of bytes remaining in the string that we are allowed
 * to pick from.  Otherwise we are allowed to pick up to the NUL that
 * would eventually appear in the string.  *remainder_p is also reduced
 * by the number of bytes we have consumed.
 *
 * If the string was not a valid UTF-8, *start pointer is set to NULL
 * and the return value is undefined.
 */
static ucs_char_t pick_one_utf8_char(const char **start, size_t *remainder_p)
{
	unsigned char *s = (unsigned char *)*start;
	ucs_char_t ch;
	size_t remainder, incr;

	/*
	 * A caller that assumes NUL terminated text can choose
	 * not to bother with the remainder length.  We will
	 * stop at the first NUL.
	 */
	remainder = (remainder_p ? *remainder_p : 999);

	if (remainder < 1) {
		goto invalid;
	} else if (*s < 0x80) {
		/* 0xxxxxxx */
		ch = *s;
		incr = 1;
	} else if ((s[0] & 0xe0) == 0xc0) {
		/* 110XXXXx 10xxxxxx */
		if (remainder < 2 ||
		    (s[1] & 0xc0) != 0x80 ||
		    (s[0] & 0xfe) == 0xc0)
			goto invalid;
		ch = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f);
		incr = 2;
	} else if ((s[0] & 0xf0) == 0xe0) {
		/* 1110XXXX 10Xxxxxx 10xxxxxx */
		if (remainder < 3 ||
		    (s[1] & 0xc0) != 0x80 ||
		    (s[2] & 0xc0) != 0x80 ||
		    /* overlong? */
		    (s[0] == 0xe0 && (s[1] & 0xe0) == 0x80) ||
		    /* surrogate? */
		    (s[0] == 0xed && (s[1] & 0xe0) == 0xa0) ||
		    /* U+FFFE or U+FFFF? */
		    (s[0] == 0xef && s[1] == 0xbf &&
		     (s[2] & 0xfe) == 0xbe))
			goto invalid;
		ch = ((s[0] & 0x0f) << 12) |
			((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
		incr = 3;
	} else if ((s[0] & 0xf8) == 0xf0) {
		/* 11110XXX 10XXxxxx 10xxxxxx 10xxxxxx */
		if (remainder < 4 ||
		    (s[1] & 0xc0) != 0x80 ||
		    (s[2] & 0xc0) != 0x80 ||
		    (s[3] & 0xc0) != 0x80 ||
		    /* overlong? */
		    (s[0] == 0xf0 && (s[1] & 0xf0) == 0x80) ||
		    /* > U+10FFFF? */
		    (s[0] == 0xf4 && s[1] > 0x8f) || s[0] > 0xf4)
			goto invalid;
		ch = ((s[0] & 0x07) << 18) | ((s[1] & 0x3f) << 12) |
			((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
		incr = 4;
	} else {
invalid:
		*start = NULL;
		return 0;
	}

	*start += incr;
	if (remainder_p)
		*remainder_p = remainder - incr;
	return ch;
}

/*
 * This function returns the number of columns occupied by the character
 * pointed to by the variable start. The pointer is updated to point at
 * the next character. When remainder_p is not NULL, it points at the
 * location that stores the number of remaining bytes we can use to pick
 * a character (see pick_one_utf8_char() above).
 */
int utf8_width(const char **start, size_t *remainder_p)
{
	ucs_char_t ch = pick_one_utf8_char(start, remainder_p);
	if (!*start)
		return 0;
	return git_wcwidth(ch);
}

/*
 * Returns the total number of columns required by a null-terminated
 * string, assuming that the string is utf8.  Returns strlen() instead
 * if the string does not look like a valid utf8 string.
 */
int utf8_strnwidth(const char *string, int len, int skip_ansi)
{
	int width = 0;
	const char *orig = string;

	if (len == -1)
		len = strlen(string);
	while (string && string < orig + len) {
		int skip;
		while (skip_ansi &&
		       (skip = display_mode_esc_sequence_len(string)) != 0)
			string += skip;
		width += utf8_width(&string, NULL);
	}
	return string ? width : len;
}

int utf8_strwidth(const char *string)
{
	return utf8_strnwidth(string, -1, 0);
}

int is_utf8(const char *text)
{
	while (*text) {
		if (*text == '\n' || *text == '\t' || *text == '\r') {
			text++;
			continue;
		}
		utf8_width(&text, NULL);
		if (!text)
			return 0;
	}
	return 1;
}

static void strbuf_addchars(struct strbuf *sb, int c, size_t n)
{
	strbuf_grow(sb, n);
	memset(sb->buf + sb->len, c, n);
	strbuf_setlen(sb, sb->len + n);
}

static void strbuf_add_indented_text(struct strbuf *buf, const char *text,
				     int indent, int indent2)
{
	if (indent < 0)
		indent = 0;
	while (*text) {
		const char *eol = strchrnul(text, '\n');
		if (*eol == '\n')
			eol++;
		strbuf_addchars(buf, ' ', indent);
		strbuf_add(buf, text, eol - text);
		text = eol;
		indent = indent2;
	}
}

/*
 * Wrap the text, if necessary. The variable indent is the indent for the
 * first line, indent2 is the indent for all other lines.
 * If indent is negative, assume that already -indent columns have been
 * consumed (and no extra indent is necessary for the first line).
 */
void strbuf_add_wrapped_text(struct strbuf *buf,
		const char *text, int indent1, int indent2, int width)
{
	int indent, w, assume_utf8 = 1;
	const char *bol, *space, *start = text;
	size_t orig_len = buf->len;

	if (width <= 0) {
		strbuf_add_indented_text(buf, text, indent1, indent2);
		return;
	}

retry:
	bol = text;
	w = indent = indent1;
	space = NULL;
	if (indent < 0) {
		w = -indent;
		space = text;
	}

	for (;;) {
		char c;
		size_t skip;

		while ((skip = display_mode_esc_sequence_len(text)))
			text += skip;

		c = *text;
		if (!c || isspace(c)) {
			if (w <= width || !space) {
				const char *start = bol;
				if (!c && text == start)
					return;
				if (space)
					start = space;
				else
					strbuf_addchars(buf, ' ', indent);
				strbuf_add(buf, start, text - start);
				if (!c)
					return;
				space = text;
				if (c == '\t')
					w |= 0x07;
				else if (c == '\n') {
					space++;
					if (*space == '\n') {
						strbuf_addch(buf, '\n');
						goto new_line;
					}
					else if (!isalnum(*space))
						goto new_line;
					else
						strbuf_addch(buf, ' ');
				}
				w++;
				text++;
			}
			else {
new_line:
				strbuf_addch(buf, '\n');
				text = bol = space + isspace(*space);
				space = NULL;
				w = indent = indent2;
			}
			continue;
		}
		if (assume_utf8) {
			w += utf8_width(&text, NULL);
			if (!text) {
				assume_utf8 = 0;
				text = start;
				strbuf_setlen(buf, orig_len);
				goto retry;
			}
		} else {
			w++;
			text++;
		}
	}
}

void strbuf_add_wrapped_bytes(struct strbuf *buf, const char *data, int len,
			     int indent, int indent2, int width)
{
	char *tmp = xstrndup(data, len);
	strbuf_add_wrapped_text(buf, tmp, indent, indent2, width);
	free(tmp);
}

void strbuf_utf8_replace(struct strbuf *sb_src, int pos, int width,
			 const char *subst)
{
	struct strbuf sb_dst = STRBUF_INIT;
	char *src = sb_src->buf;
	char *end = src + sb_src->len;
	char *dst;
	int w = 0, subst_len = 0;

	if (subst)
		subst_len = strlen(subst);
	strbuf_grow(&sb_dst, sb_src->len + subst_len);
	dst = sb_dst.buf;

	while (src < end) {
		char *old;
		size_t n;

		while ((n = display_mode_esc_sequence_len(src))) {
			memcpy(dst, src, n);
			src += n;
			dst += n;
		}

		old = src;
		n = utf8_width((const char**)&src, NULL);
		if (!src) 	/* broken utf-8, do nothing */
			return;
		if (n && w >= pos && w < pos + width) {
			if (subst) {
				memcpy(dst, subst, subst_len);
				dst += subst_len;
				subst = NULL;
			}
			w += n;
			continue;
		}
		memcpy(dst, old, src - old);
		dst += src - old;
		w += n;
	}
	strbuf_setlen(&sb_dst, dst - sb_dst.buf);
	strbuf_swap(sb_src, &sb_dst);
	strbuf_release(&sb_dst);
}

int is_encoding_utf8(const char *name)
{
	if (!name)
		return 1;
	if (!strcasecmp(name, "utf-8") || !strcasecmp(name, "utf8"))
		return 1;
	return 0;
}

int same_encoding(const char *src, const char *dst)
{
	if (is_encoding_utf8(src) && is_encoding_utf8(dst))
		return 1;
	return !strcasecmp(src, dst);
}

/*
 * Wrapper for fprintf and returns the total number of columns required
 * for the printed string, assuming that the string is utf8.
 */
int utf8_fprintf(FILE *stream, const char *format, ...)
{
	struct strbuf buf = STRBUF_INIT;
	va_list arg;
	int columns;

	va_start(arg, format);
	strbuf_vaddf(&buf, format, arg);
	va_end(arg);

	columns = fputs(buf.buf, stream);
	if (0 <= columns) /* keep the error from the I/O */
		columns = utf8_strwidth(buf.buf);
	strbuf_release(&buf);
	return columns;
}

/*
 * Given a buffer and its encoding, return it re-encoded
 * with iconv.  If the conversion fails, returns NULL.
 */
#ifndef NO_ICONV
#if defined(OLD_ICONV) || (defined(__sun__) && !defined(_XPG6))
	typedef const char * iconv_ibp;
#else
	typedef char * iconv_ibp;
#endif
char *reencode_string_iconv(const char *in, size_t insz, iconv_t conv, int *outsz_p)
{
	size_t outsz, outalloc;
	char *out, *outpos;
	iconv_ibp cp;

	outsz = insz;
	outalloc = outsz + 1; /* for terminating NUL */
	out = xmalloc(outalloc);
	outpos = out;
	cp = (iconv_ibp)in;

	while (1) {
		size_t cnt = iconv(conv, &cp, &insz, &outpos, &outsz);

		if (cnt == (size_t) -1) {
			size_t sofar;
			if (errno != E2BIG) {
				free(out);
				return NULL;
			}
			/* insz has remaining number of bytes.
			 * since we started outsz the same as insz,
			 * it is likely that insz is not enough for
			 * converting the rest.
			 */
			sofar = outpos - out;
			outalloc = sofar + insz * 2 + 32;
			out = xrealloc(out, outalloc);
			outpos = out + sofar;
			outsz = outalloc - sofar - 1;
		}
		else {
			*outpos = '\0';
			if (outsz_p)
				*outsz_p = outpos - out;
			break;
		}
	}
	return out;
}

char *reencode_string_len(const char *in, int insz,
			  const char *out_encoding, const char *in_encoding,
			  int *outsz)
{
	iconv_t conv;
	char *out;

	if (!in_encoding)
		return NULL;

	conv = iconv_open(out_encoding, in_encoding);
	if (conv == (iconv_t) -1) {
		/*
		 * Some platforms do not have the variously spelled variants of
		 * UTF-8, so let's fall back to trying the most official
		 * spelling. We do so only as a fallback in case the platform
		 * does understand the user's spelling, but not our official
		 * one.
		 */
		if (is_encoding_utf8(in_encoding))
			in_encoding = "UTF-8";
		if (is_encoding_utf8(out_encoding))
			out_encoding = "UTF-8";
		conv = iconv_open(out_encoding, in_encoding);
		if (conv == (iconv_t) -1)
			return NULL;
	}

	out = reencode_string_iconv(in, insz, conv, outsz);
	iconv_close(conv);
	return out;
}
#endif

/*
 * Returns first character length in bytes for multi-byte `text` according to
 * `encoding`.
 *
 * - The `text` pointer is updated to point at the next character.
 * - When `remainder_p` is not NULL, on entry `*remainder_p` is how much bytes
 *   we can consume from text, and on exit `*remainder_p` is reduced by returned
 *   character length. Otherwise `text` is treated as limited by NUL.
 */
int mbs_chrlen(const char **text, size_t *remainder_p, const char *encoding)
{
	int chrlen;
	const char *p = *text;
	size_t r = (remainder_p ? *remainder_p : SIZE_MAX);

	if (r < 1)
		return 0;

	if (is_encoding_utf8(encoding)) {
		pick_one_utf8_char(&p, &r);

		chrlen = p ? (p - *text)
			   : 1 /* not valid UTF-8 -> raw byte sequence */;
	}
	else {
		/*
		 * TODO use iconv to decode one char and obtain its chrlen
		 * for now, let's treat encodings != UTF-8 as one-byte
		 */
		chrlen = 1;
	}

	*text += chrlen;
	if (remainder_p)
		*remainder_p -= chrlen;

	return chrlen;
}
