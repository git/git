#ifndef GIT_UTF8_H
#define GIT_UTF8_H

typedef unsigned int ucs_char_t;  /* assuming 32bit int */

size_t display_mode_esc_sequence_len(const char *s);
int utf8_width(const char **start, size_t *remainder_p);
int utf8_strnwidth(const char *string, int len, int skip_ansi);
int utf8_strwidth(const char *string);
int is_utf8(const char *text);
int is_encoding_utf8(const char *name);
int same_encoding(const char *, const char *);
__attribute__((format (printf, 2, 3)))
int utf8_fprintf(FILE *, const char *, ...);

extern const char utf8_bom[];
extern int skip_utf8_bom(char **, size_t);

void strbuf_add_wrapped_text(struct strbuf *buf,
		const char *text, int indent, int indent2, int width);
void strbuf_add_wrapped_bytes(struct strbuf *buf, const char *data, int len,
			     int indent, int indent2, int width);
void strbuf_utf8_replace(struct strbuf *sb, int pos, int width,
			 const char *subst);

#ifndef NO_ICONV
char *reencode_string_iconv(const char *in, size_t insz,
			    iconv_t conv, int *outsz);
char *reencode_string_len(const char *in, int insz,
			  const char *out_encoding,
			  const char *in_encoding,
			  int *outsz);
#else
static inline char *reencode_string_len(const char *a, int b,
					const char *c, const char *d, int *e)
{ if (e) *e = 0; return NULL; }
#endif

static inline char *reencode_string(const char *in,
				    const char *out_encoding,
				    const char *in_encoding)
{
	return reencode_string_len(in, strlen(in),
				   out_encoding, in_encoding,
				   NULL);
}

int mbs_chrlen(const char **text, size_t *remainder_p, const char *encoding);

/*
 * Returns true if the path would match ".git" after HFS case-folding.
 * The path should be NUL-terminated, but we will match variants of both ".git\0"
 * and ".git/..." (but _not_ ".../.git"). This makes it suitable for both fsck
 * and verify_path().
 */
int is_hfs_dotgit(const char *path);

typedef enum {
	ALIGN_LEFT,
	ALIGN_MIDDLE,
	ALIGN_RIGHT
} align_type;

/*
 * Align the string given and store it into a strbuf as per the
 * 'position' and 'width'. If the given string length is larger than
 * 'width' than then the input string is not truncated and no
 * alignment is done.
 */
void strbuf_utf8_align(struct strbuf *buf, align_type position, unsigned int width,
		       const char *s);

/*
 * If a data stream is declared as UTF-16BE or UTF-16LE, then a UTF-16
 * BOM must not be used [1]. The same applies for the UTF-32 equivalents.
 * The function returns true if this rule is violated.
 *
 * [1] http://unicode.org/faq/utf_bom.html#bom10
 */
int has_prohibited_utf_bom(const char *enc, const char *data, size_t len);

/*
 * If the endianness is not defined in the encoding name, then we
 * require a BOM. The function returns true if a required BOM is missing.
 *
 * The Unicode standard instructs to assume big-endian if there in no
 * BOM for UTF-16/32 [1][2]. However, the W3C/WHATWG encoding standard
 * used in HTML5 recommends to assume little-endian to "deal with
 * deployed content" [3].
 *
 * Therefore, strictly requiring a BOM seems to be the safest option for
 * content in Git.
 *
 * [1] http://unicode.org/faq/utf_bom.html#gen6
 * [2] http://www.unicode.org/versions/Unicode10.0.0/ch03.pdf
 *     Section 3.10, D98, page 132
 * [3] https://encoding.spec.whatwg.org/#utf-16le
 */
int is_missing_required_utf_bom(const char *enc, const char *data, size_t len);

#endif
