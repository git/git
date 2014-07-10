#ifndef STRBUF_H
#define STRBUF_H

/* See Documentation/technical/api-strbuf.txt */

extern char strbuf_slopbuf[];
struct strbuf {
	size_t alloc;
	size_t len;
	char *buf;
};

#define STRBUF_INIT  { 0, 0, strbuf_slopbuf }

/*----- strbuf life cycle -----*/
extern void strbuf_init(struct strbuf *, size_t);
extern void strbuf_release(struct strbuf *);
extern char *strbuf_detach(struct strbuf *, size_t *);
extern void strbuf_attach(struct strbuf *, void *, size_t, size_t);
static inline void strbuf_swap(struct strbuf *a, struct strbuf *b)
{
	struct strbuf tmp = *a;
	*a = *b;
	*b = tmp;
}

/*----- strbuf size related -----*/
static inline size_t strbuf_avail(const struct strbuf *sb)
{
	return sb->alloc ? sb->alloc - sb->len - 1 : 0;
}

extern void strbuf_grow(struct strbuf *, size_t);

static inline void strbuf_setlen(struct strbuf *sb, size_t len)
{
	if (len > (sb->alloc ? sb->alloc - 1 : 0))
		die("BUG: strbuf_setlen() beyond buffer");
	sb->len = len;
	sb->buf[len] = '\0';
}
#define strbuf_reset(sb)  strbuf_setlen(sb, 0)

/*----- content related -----*/
extern void strbuf_trim(struct strbuf *);
extern void strbuf_rtrim(struct strbuf *);
extern void strbuf_ltrim(struct strbuf *);
extern int strbuf_reencode(struct strbuf *sb, const char *from, const char *to);
extern void strbuf_tolower(struct strbuf *sb);
extern int strbuf_cmp(const struct strbuf *, const struct strbuf *);

static inline int strbuf_strip_suffix(struct strbuf *sb, const char *suffix)
{
	if (strip_suffix_mem(sb->buf, &sb->len, suffix)) {
		strbuf_setlen(sb, sb->len);
		return 1;
	} else
		return 0;
}

/*
 * Split str (of length slen) at the specified terminator character.
 * Return a null-terminated array of pointers to strbuf objects
 * holding the substrings.  The substrings include the terminator,
 * except for the last substring, which might be unterminated if the
 * original string did not end with a terminator.  If max is positive,
 * then split the string into at most max substrings (with the last
 * substring containing everything following the (max-1)th terminator
 * character).
 *
 * For lighter-weight alternatives, see string_list_split() and
 * string_list_split_in_place().
 */
extern struct strbuf **strbuf_split_buf(const char *, size_t,
					int terminator, int max);

/*
 * Split a NUL-terminated string at the specified terminator
 * character.  See strbuf_split_buf() for more information.
 */
static inline struct strbuf **strbuf_split_str(const char *str,
					       int terminator, int max)
{
	return strbuf_split_buf(str, strlen(str), terminator, max);
}

/*
 * Split a strbuf at the specified terminator character.  See
 * strbuf_split_buf() for more information.
 */
static inline struct strbuf **strbuf_split_max(const struct strbuf *sb,
						int terminator, int max)
{
	return strbuf_split_buf(sb->buf, sb->len, terminator, max);
}

/*
 * Split a strbuf at the specified terminator character.  See
 * strbuf_split_buf() for more information.
 */
static inline struct strbuf **strbuf_split(const struct strbuf *sb,
					   int terminator)
{
	return strbuf_split_max(sb, terminator, 0);
}

/*
 * Free a NULL-terminated list of strbufs (for example, the return
 * values of the strbuf_split*() functions).
 */
extern void strbuf_list_free(struct strbuf **);

/*----- add data in your buffer -----*/
static inline void strbuf_addch(struct strbuf *sb, int c)
{
	strbuf_grow(sb, 1);
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = '\0';
}

extern void strbuf_insert(struct strbuf *, size_t pos, const void *, size_t);
extern void strbuf_remove(struct strbuf *, size_t pos, size_t len);

/* splice pos..pos+len with given data */
extern void strbuf_splice(struct strbuf *, size_t pos, size_t len,
                          const void *, size_t);

extern void strbuf_add_commented_lines(struct strbuf *out, const char *buf, size_t size);

extern void strbuf_add(struct strbuf *, const void *, size_t);
static inline void strbuf_addstr(struct strbuf *sb, const char *s)
{
	strbuf_add(sb, s, strlen(s));
}
static inline void strbuf_addbuf(struct strbuf *sb, const struct strbuf *sb2)
{
	strbuf_grow(sb, sb2->len);
	strbuf_add(sb, sb2->buf, sb2->len);
}
extern void strbuf_adddup(struct strbuf *sb, size_t pos, size_t len);

typedef size_t (*expand_fn_t) (struct strbuf *sb, const char *placeholder, void *context);
extern void strbuf_expand(struct strbuf *sb, const char *format, expand_fn_t fn, void *context);
struct strbuf_expand_dict_entry {
	const char *placeholder;
	const char *value;
};
extern size_t strbuf_expand_dict_cb(struct strbuf *sb, const char *placeholder, void *context);
extern void strbuf_addbuf_percentquote(struct strbuf *dst, const struct strbuf *src);

__attribute__((format (printf,2,3)))
extern void strbuf_addf(struct strbuf *sb, const char *fmt, ...);
__attribute__((format (printf, 2, 3)))
extern void strbuf_commented_addf(struct strbuf *sb, const char *fmt, ...);
__attribute__((format (printf,2,0)))
extern void strbuf_vaddf(struct strbuf *sb, const char *fmt, va_list ap);

extern void strbuf_add_lines(struct strbuf *sb, const char *prefix, const char *buf, size_t size);

/*
 * Append s to sb, with the characters '<', '>', '&' and '"' converted
 * into XML entities.
 */
extern void strbuf_addstr_xml_quoted(struct strbuf *sb, const char *s);

static inline void strbuf_complete_line(struct strbuf *sb)
{
	if (sb->len && sb->buf[sb->len - 1] != '\n')
		strbuf_addch(sb, '\n');
}

extern size_t strbuf_fread(struct strbuf *, size_t, FILE *);
/* XXX: if read fails, any partial read is undone */
extern ssize_t strbuf_read(struct strbuf *, int fd, size_t hint);
extern int strbuf_read_file(struct strbuf *sb, const char *path, size_t hint);
extern int strbuf_readlink(struct strbuf *sb, const char *path, size_t hint);

extern int strbuf_getwholeline(struct strbuf *, FILE *, int);
extern int strbuf_getline(struct strbuf *, FILE *, int);
extern int strbuf_getwholeline_fd(struct strbuf *, int, int);

extern void stripspace(struct strbuf *buf, int skip_comments);
extern int launch_editor(const char *path, struct strbuf *buffer, const char *const *env);

extern int strbuf_branchname(struct strbuf *sb, const char *name);
extern int strbuf_check_branch_ref(struct strbuf *sb, const char *name);

extern void strbuf_addstr_urlencode(struct strbuf *, const char *,
				    int reserved);
extern void strbuf_humanise_bytes(struct strbuf *buf, off_t bytes);

__attribute__((format (printf,1,2)))
extern int printf_ln(const char *fmt, ...);
__attribute__((format (printf,2,3)))
extern int fprintf_ln(FILE *fp, const char *fmt, ...);

char *xstrdup_tolower(const char *);

/*
 * Create a newly allocated string using printf format. You can do this easily
 * with a strbuf, but this provides a shortcut to save a few lines.
 */
__attribute__((format (printf, 1, 0)))
char *xstrvfmt(const char *fmt, va_list ap);
__attribute__((format (printf, 1, 2)))
char *xstrfmt(const char *fmt, ...);

#endif /* STRBUF_H */
