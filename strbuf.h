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
static inline void strbuf_swap(struct strbuf *a, struct strbuf *b) {
	struct strbuf tmp = *a;
	*a = *b;
	*b = tmp;
}

/*----- strbuf size related -----*/
static inline size_t strbuf_avail(const struct strbuf *sb) {
	return sb->alloc ? sb->alloc - sb->len - 1 : 0;
}

extern void strbuf_grow(struct strbuf *, size_t);

static inline void strbuf_setlen(struct strbuf *sb, size_t len) {
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
extern int strbuf_cmp(const struct strbuf *, const struct strbuf *);

extern struct strbuf **strbuf_split(const struct strbuf *, int delim);
extern void strbuf_list_free(struct strbuf **);

/*----- add data in your buffer -----*/
static inline void strbuf_addch(struct strbuf *sb, int c) {
	strbuf_grow(sb, 1);
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = '\0';
}

extern void strbuf_insert(struct strbuf *, size_t pos, const void *, size_t);
extern void strbuf_remove(struct strbuf *, size_t pos, size_t len);

/* splice pos..pos+len with given data */
extern void strbuf_splice(struct strbuf *, size_t pos, size_t len,
                          const void *, size_t);

extern void strbuf_add(struct strbuf *, const void *, size_t);
static inline void strbuf_addstr(struct strbuf *sb, const char *s) {
	strbuf_add(sb, s, strlen(s));
}
static inline void strbuf_addbuf(struct strbuf *sb, const struct strbuf *sb2) {
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
__attribute__((format (printf,2,0)))
extern void strbuf_vaddf(struct strbuf *sb, const char *fmt, va_list ap);

extern size_t strbuf_fread(struct strbuf *, size_t, FILE *);
/* XXX: if read fails, any partial read is undone */
extern ssize_t strbuf_read(struct strbuf *, int fd, size_t hint);
extern int strbuf_read_file(struct strbuf *sb, const char *path, size_t hint);
extern int strbuf_readlink(struct strbuf *sb, const char *path, size_t hint);

extern int strbuf_getwholeline(struct strbuf *, FILE *, int);
extern int strbuf_getline(struct strbuf *, FILE *, int);

extern void stripspace(struct strbuf *buf, int skip_comments);
extern int launch_editor(const char *path, struct strbuf *buffer, const char *const *env);

extern int strbuf_branchname(struct strbuf *sb, const char *name);
extern int strbuf_check_branch_ref(struct strbuf *sb, const char *name);

#endif /* STRBUF_H */
