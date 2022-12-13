#include "git-compat-util.h"
#include "strbuf.h"
#include "utf8.h"

/* This code is originally from http://www.cl.cam.ac.uk/~mgk25/ucs/ */

static const char utf16_be_bom[] = {'\xFE', '\xFF'};
static const char utf16_le_bom[] = {'\xFF', '\xFE'};
static const char utf32_be_bom[] = {'\0', '\0', '\xFE', '\xFF'};
static const char utf32_le_bom[] = {'\xFF', '\xFE', '\0', '\0'};

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
		mid = min + (max - min) / 2;
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
#include "unicode-width.h"

	/* test for 8-bit control characters */
	if (ch == 0)
		return 0;
	if (ch < 32 || (ch >= 0x7f && ch < 0xa0))
		return -1;

	/* binary search in table of non-spacing characters */
	if (bisearch(ch, zero_width, ARRAY_SIZE(zero_width) - 1))
		return 0;

	/* binary search in table of double width characters */
	if (bisearch(ch, double_width, ARRAY_SIZE(double_width) - 1))
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
int utf8_strnwidth(const char *string, size_t len, int skip_ansi)
{
	const char *orig = string;
	size_t width = 0;

	while (string && string < orig + len) {
		int glyph_width;
		size_t skip;

		while (skip_ansi &&
		       (skip = display_mode_esc_sequence_len(string)) != 0)
			string += skip;

		glyph_width = utf8_width(&string, NULL);
		if (glyph_width > 0)
			width += glyph_width;
	}

	/*
	 * TODO: fix the interface of this function and `utf8_strwidth()` to
	 * return `size_t` instead of `int`.
	 */
	return cast_size_t_to_int(string ? width : len);
}

int utf8_strwidth(const char *string)
{
	return utf8_strnwidth(string, strlen(string), 0);
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
	const char *src = sb_src->buf, *end = sb_src->buf + sb_src->len;
	struct strbuf dst;
	int w = 0;

	strbuf_init(&dst, sb_src->len);

	while (src < end) {
		const char *old;
		int glyph_width;
		size_t n;

		while ((n = display_mode_esc_sequence_len(src))) {
			strbuf_add(&dst, src, n);
			src += n;
		}

		if (src >= end)
			break;

		old = src;
		glyph_width = utf8_width((const char**)&src, NULL);
		if (!src) /* broken utf-8, do nothing */
			goto out;

		/*
		 * In case we see a control character we copy it into the
		 * buffer, but don't add it to the width.
		 */
		if (glyph_width < 0)
			glyph_width = 0;

		if (glyph_width && w >= pos && w < pos + width) {
			if (subst) {
				strbuf_addstr(&dst, subst);
				subst = NULL;
			}
		} else {
			strbuf_add(&dst, old, src - old);
		}

		w += glyph_width;
	}

	strbuf_swap(sb_src, &dst);
out:
	strbuf_release(&dst);
}

/*
 * Returns true (1) if the src encoding name matches the dst encoding
 * name directly or one of its alternative names. E.g. UTF-16BE is the
 * same as UTF16BE.
 */
static int same_utf_encoding(const char *src, const char *dst)
{
	if (skip_iprefix(src, "utf", &src) && skip_iprefix(dst, "utf", &dst)) {
		skip_prefix(src, "-", &src);
		skip_prefix(dst, "-", &dst);
		return !strcasecmp(src, dst);
	}
	return 0;
}

int is_encoding_utf8(const char *name)
{
	if (!name)
		return 1;
	if (same_utf_encoding("utf-8", name))
		return 1;
	return 0;
}

int same_encoding(const char *src, const char *dst)
{
	static const char utf8[] = "UTF-8";

	if (!src)
		src = utf8;
	if (!dst)
		dst = utf8;
	if (same_utf_encoding(src, dst))
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
char *reencode_string_iconv(const char *in, size_t insz, iconv_t conv,
			    size_t bom_len, size_t *outsz_p)
{
	size_t outsz, outalloc;
	char *out, *outpos;
	iconv_ibp cp;

	outsz = insz;
	outalloc = st_add(outsz, 1 + bom_len); /* for terminating NUL */
	out = xmalloc(outalloc);
	outpos = out + bom_len;
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
			outalloc = st_add3(sofar, st_mult(insz, 2), 32);
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

static const char *fallback_encoding(const char *name)
{
	/*
	 * Some platforms do not have the variously spelled variants of
	 * UTF-8, so let's fall back to trying the most official
	 * spelling. We do so only as a fallback in case the platform
	 * does understand the user's spelling, but not our official
	 * one.
	 */
	if (is_encoding_utf8(name))
		return "UTF-8";

	/*
	 * Even though latin-1 is still seen in e-mail
	 * headers, some platforms only install ISO-8859-1.
	 */
	if (!strcasecmp(name, "latin-1"))
		return "ISO-8859-1";

	return name;
}

char *reencode_string_len(const char *in, size_t insz,
			  const char *out_encoding, const char *in_encoding,
			  size_t *outsz)
{
	iconv_t conv;
	char *out;
	const char *bom_str = NULL;
	size_t bom_len = 0;

	if (!in_encoding)
		return NULL;

	/* UTF-16LE-BOM is the same as UTF-16 for reading */
	if (same_utf_encoding("UTF-16LE-BOM", in_encoding))
		in_encoding = "UTF-16";

	/*
	 * For writing, UTF-16 iconv typically creates "UTF-16BE-BOM"
	 * Some users under Windows want the little endian version
	 *
	 * We handle UTF-16 and UTF-32 ourselves only if the platform does not
	 * provide a BOM (which we require), since we want to match the behavior
	 * of the system tools and libc as much as possible.
	 */
	if (same_utf_encoding("UTF-16LE-BOM", out_encoding)) {
		bom_str = utf16_le_bom;
		bom_len = sizeof(utf16_le_bom);
		out_encoding = "UTF-16LE";
	} else if (same_utf_encoding("UTF-16BE-BOM", out_encoding)) {
		bom_str = utf16_be_bom;
		bom_len = sizeof(utf16_be_bom);
		out_encoding = "UTF-16BE";
#ifdef ICONV_OMITS_BOM
	} else if (same_utf_encoding("UTF-16", out_encoding)) {
		bom_str = utf16_be_bom;
		bom_len = sizeof(utf16_be_bom);
		out_encoding = "UTF-16BE";
	} else if (same_utf_encoding("UTF-32", out_encoding)) {
		bom_str = utf32_be_bom;
		bom_len = sizeof(utf32_be_bom);
		out_encoding = "UTF-32BE";
#endif
	}

	conv = iconv_open(out_encoding, in_encoding);
	if (conv == (iconv_t) -1) {
		in_encoding = fallback_encoding(in_encoding);
		out_encoding = fallback_encoding(out_encoding);

		conv = iconv_open(out_encoding, in_encoding);
		if (conv == (iconv_t) -1)
			return NULL;
	}
	out = reencode_string_iconv(in, insz, conv, bom_len, outsz);
	iconv_close(conv);
	if (out && bom_str && bom_len)
		memcpy(out, bom_str, bom_len);
	return out;
}
#endif

static int has_bom_prefix(const char *data, size_t len,
			  const char *bom, size_t bom_len)
{
	return data && bom && (len >= bom_len) && !memcmp(data, bom, bom_len);
}

int has_prohibited_utf_bom(const char *enc, const char *data, size_t len)
{
	return (
	  (same_utf_encoding("UTF-16BE", enc) ||
	   same_utf_encoding("UTF-16LE", enc)) &&
	  (has_bom_prefix(data, len, utf16_be_bom, sizeof(utf16_be_bom)) ||
	   has_bom_prefix(data, len, utf16_le_bom, sizeof(utf16_le_bom)))
	) || (
	  (same_utf_encoding("UTF-32BE",  enc) ||
	   same_utf_encoding("UTF-32LE", enc)) &&
	  (has_bom_prefix(data, len, utf32_be_bom, sizeof(utf32_be_bom)) ||
	   has_bom_prefix(data, len, utf32_le_bom, sizeof(utf32_le_bom)))
	);
}

int is_missing_required_utf_bom(const char *enc, const char *data, size_t len)
{
	return (
	   (same_utf_encoding(enc, "UTF-16")) &&
	   !(has_bom_prefix(data, len, utf16_be_bom, sizeof(utf16_be_bom)) ||
	     has_bom_prefix(data, len, utf16_le_bom, sizeof(utf16_le_bom)))
	) || (
	   (same_utf_encoding(enc, "UTF-32")) &&
	   !(has_bom_prefix(data, len, utf32_be_bom, sizeof(utf32_be_bom)) ||
	     has_bom_prefix(data, len, utf32_le_bom, sizeof(utf32_le_bom)))
	);
}

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

/*
 * Pick the next char from the stream, ignoring codepoints an HFS+ would.
 * Note that this is _not_ complete by any means. It's just enough
 * to make is_hfs_dotgit() work, and should not be used otherwise.
 */
static ucs_char_t next_hfs_char(const char **in)
{
	while (1) {
		ucs_char_t out = pick_one_utf8_char(in, NULL);
		/*
		 * check for malformed utf8. Technically this
		 * gets converted to a percent-sequence, but
		 * returning 0 is good enough for is_hfs_dotgit
		 * to realize it cannot be .git
		 */
		if (!*in)
			return 0;

		/* these code points are ignored completely */
		switch (out) {
		case 0x200c: /* ZERO WIDTH NON-JOINER */
		case 0x200d: /* ZERO WIDTH JOINER */
		case 0x200e: /* LEFT-TO-RIGHT MARK */
		case 0x200f: /* RIGHT-TO-LEFT MARK */
		case 0x202a: /* LEFT-TO-RIGHT EMBEDDING */
		case 0x202b: /* RIGHT-TO-LEFT EMBEDDING */
		case 0x202c: /* POP DIRECTIONAL FORMATTING */
		case 0x202d: /* LEFT-TO-RIGHT OVERRIDE */
		case 0x202e: /* RIGHT-TO-LEFT OVERRIDE */
		case 0x206a: /* INHIBIT SYMMETRIC SWAPPING */
		case 0x206b: /* ACTIVATE SYMMETRIC SWAPPING */
		case 0x206c: /* INHIBIT ARABIC FORM SHAPING */
		case 0x206d: /* ACTIVATE ARABIC FORM SHAPING */
		case 0x206e: /* NATIONAL DIGIT SHAPES */
		case 0x206f: /* NOMINAL DIGIT SHAPES */
		case 0xfeff: /* ZERO WIDTH NO-BREAK SPACE */
			continue;
		}

		return out;
	}
}

static int is_hfs_dot_generic(const char *path,
			      const char *needle, size_t needle_len)
{
	ucs_char_t c;

	c = next_hfs_char(&path);
	if (c != '.')
		return 0;

	/*
	 * there's a great deal of other case-folding that occurs
	 * in HFS+, but this is enough to catch our fairly vanilla
	 * hard-coded needles.
	 */
	for (; needle_len > 0; needle++, needle_len--) {
		c = next_hfs_char(&path);

		/*
		 * We know our needles contain only ASCII, so we clamp here to
		 * make the results of tolower() sane.
		 */
		if (c > 127)
			return 0;
		if (tolower(c) != *needle)
			return 0;
	}

	c = next_hfs_char(&path);
	if (c && !is_dir_sep(c))
		return 0;

	return 1;
}

/*
 * Inline wrapper to make sure the compiler resolves strlen() on literals at
 * compile time.
 */
static inline int is_hfs_dot_str(const char *path, const char *needle)
{
	return is_hfs_dot_generic(path, needle, strlen(needle));
}

int is_hfs_dotgit(const char *path)
{
	return is_hfs_dot_str(path, "git");
}

int is_hfs_dotgitmodules(const char *path)
{
	return is_hfs_dot_str(path, "gitmodules");
}

int is_hfs_dotgitignore(const char *path)
{
	return is_hfs_dot_str(path, "gitignore");
}

int is_hfs_dotgitattributes(const char *path)
{
	return is_hfs_dot_str(path, "gitattributes");
}

int is_hfs_dotmailmap(const char *path)
{
	return is_hfs_dot_str(path, "mailmap");
}

const char utf8_bom[] = "\357\273\277";

int skip_utf8_bom(char **text, size_t len)
{
	if (len < strlen(utf8_bom) ||
	    memcmp(*text, utf8_bom, strlen(utf8_bom)))
		return 0;
	*text += strlen(utf8_bom);
	return 1;
}

void strbuf_utf8_align(struct strbuf *buf, align_type position, unsigned int width,
		       const char *s)
{
	size_t slen = strlen(s);
	int display_len = utf8_strnwidth(s, slen, 0);
	int utf8_compensation = slen - display_len;

	if (display_len >= width) {
		strbuf_addstr(buf, s);
		return;
	}

	if (position == ALIGN_LEFT)
		strbuf_addf(buf, "%-*s", width + utf8_compensation, s);
	else if (position == ALIGN_MIDDLE) {
		int left = (width - display_len) / 2;
		strbuf_addf(buf, "%*s%-*s", left, "", width - left + utf8_compensation, s);
	} else if (position == ALIGN_RIGHT)
		strbuf_addf(buf, "%*s", width + utf8_compensation, s);
}
