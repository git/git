#include "git-compat-util.h"
#include "abspath.h"
#include "alloc.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "object-name.h"
#include "refs.h"
#include "string-list.h"
#include "utf8.h"
#include "date.h"
#include "wrapper.h"

int starts_with(const char *str, const char *prefix)
{
	for (; ; str++, prefix++)
		if (!*prefix)
			return 1;
		else if (*str != *prefix)
			return 0;
}

int istarts_with(const char *str, const char *prefix)
{
	for (; ; str++, prefix++)
		if (!*prefix)
			return 1;
		else if (tolower(*str) != tolower(*prefix))
			return 0;
}

int skip_to_optional_arg_default(const char *str, const char *prefix,
				 const char **arg, const char *def)
{
	const char *p;

	if (!skip_prefix(str, prefix, &p))
		return 0;

	if (!*p) {
		if (arg)
			*arg = def;
		return 1;
	}

	if (*p != '=')
		return 0;

	if (arg)
		*arg = p + 1;
	return 1;
}

/*
 * Used as the default ->buf value, so that people can always assume
 * buf is non NULL and ->buf is NUL terminated even for a freshly
 * initialized strbuf.
 */
char strbuf_slopbuf[1];

void strbuf_init(struct strbuf *sb, size_t hint)
{
	struct strbuf blank = STRBUF_INIT;
	memcpy(sb, &blank, sizeof(*sb));
	if (hint)
		strbuf_grow(sb, hint);
}

void strbuf_release(struct strbuf *sb)
{
	if (sb->alloc) {
		free(sb->buf);
		strbuf_init(sb, 0);
	}
}

char *strbuf_detach(struct strbuf *sb, size_t *sz)
{
	char *res;
	strbuf_grow(sb, 0);
	res = sb->buf;
	if (sz)
		*sz = sb->len;
	strbuf_init(sb, 0);
	return res;
}

void strbuf_attach(struct strbuf *sb, void *buf, size_t len, size_t alloc)
{
	strbuf_release(sb);
	sb->buf   = buf;
	sb->len   = len;
	sb->alloc = alloc;
	strbuf_grow(sb, 0);
	sb->buf[sb->len] = '\0';
}

void strbuf_grow(struct strbuf *sb, size_t extra)
{
	int new_buf = !sb->alloc;
	if (unsigned_add_overflows(extra, 1) ||
	    unsigned_add_overflows(sb->len, extra + 1))
		die("you want to use way too much memory");
	if (new_buf)
		sb->buf = NULL;
	ALLOC_GROW(sb->buf, sb->len + extra + 1, sb->alloc);
	if (new_buf)
		sb->buf[0] = '\0';
}

void strbuf_trim(struct strbuf *sb)
{
	strbuf_rtrim(sb);
	strbuf_ltrim(sb);
}

void strbuf_rtrim(struct strbuf *sb)
{
	while (sb->len > 0 && isspace((unsigned char)sb->buf[sb->len - 1]))
		sb->len--;
	sb->buf[sb->len] = '\0';
}

void strbuf_trim_trailing_dir_sep(struct strbuf *sb)
{
	while (sb->len > 0 && is_dir_sep((unsigned char)sb->buf[sb->len - 1]))
		sb->len--;
	sb->buf[sb->len] = '\0';
}

void strbuf_trim_trailing_newline(struct strbuf *sb)
{
	if (sb->len > 0 && sb->buf[sb->len - 1] == '\n') {
		if (--sb->len > 0 && sb->buf[sb->len - 1] == '\r')
			--sb->len;
		sb->buf[sb->len] = '\0';
	}
}

void strbuf_ltrim(struct strbuf *sb)
{
	char *b = sb->buf;
	while (sb->len > 0 && isspace(*b)) {
		b++;
		sb->len--;
	}
	memmove(sb->buf, b, sb->len);
	sb->buf[sb->len] = '\0';
}

int strbuf_reencode(struct strbuf *sb, const char *from, const char *to)
{
	char *out;
	size_t len;

	if (same_encoding(from, to))
		return 0;

	out = reencode_string_len(sb->buf, sb->len, to, from, &len);
	if (!out)
		return -1;

	strbuf_attach(sb, out, len, len);
	return 0;
}

void strbuf_tolower(struct strbuf *sb)
{
	char *p = sb->buf, *end = sb->buf + sb->len;
	for (; p < end; p++)
		*p = tolower(*p);
}

struct strbuf **strbuf_split_buf(const char *str, size_t slen,
				 int terminator, int max)
{
	struct strbuf **ret = NULL;
	size_t nr = 0, alloc = 0;
	struct strbuf *t;

	while (slen) {
		int len = slen;
		if (max <= 0 || nr + 1 < max) {
			const char *end = memchr(str, terminator, slen);
			if (end)
				len = end - str + 1;
		}
		t = xmalloc(sizeof(struct strbuf));
		strbuf_init(t, len);
		strbuf_add(t, str, len);
		ALLOC_GROW(ret, nr + 2, alloc);
		ret[nr++] = t;
		str += len;
		slen -= len;
	}
	ALLOC_GROW(ret, nr + 1, alloc); /* In case string was empty */
	ret[nr] = NULL;
	return ret;
}

void strbuf_add_separated_string_list(struct strbuf *str,
				      const char *sep,
				      struct string_list *slist)
{
	struct string_list_item *item;
	int sep_needed = 0;

	for_each_string_list_item(item, slist) {
		if (sep_needed)
			strbuf_addstr(str, sep);
		strbuf_addstr(str, item->string);
		sep_needed = 1;
	}
}

void strbuf_list_free(struct strbuf **sbs)
{
	struct strbuf **s = sbs;

	if (!s)
		return;
	while (*s) {
		strbuf_release(*s);
		free(*s++);
	}
	free(sbs);
}

int strbuf_cmp(const struct strbuf *a, const struct strbuf *b)
{
	size_t len = a->len < b->len ? a->len: b->len;
	int cmp = memcmp(a->buf, b->buf, len);
	if (cmp)
		return cmp;
	return a->len < b->len ? -1: a->len != b->len;
}

void strbuf_splice(struct strbuf *sb, size_t pos, size_t len,
				   const void *data, size_t dlen)
{
	if (unsigned_add_overflows(pos, len))
		die("you want to use way too much memory");
	if (pos > sb->len)
		die("`pos' is too far after the end of the buffer");
	if (pos + len > sb->len)
		die("`pos + len' is too far after the end of the buffer");

	if (dlen >= len)
		strbuf_grow(sb, dlen - len);
	memmove(sb->buf + pos + dlen,
			sb->buf + pos + len,
			sb->len - pos - len);
	memcpy(sb->buf + pos, data, dlen);
	strbuf_setlen(sb, sb->len + dlen - len);
}

void strbuf_insert(struct strbuf *sb, size_t pos, const void *data, size_t len)
{
	strbuf_splice(sb, pos, 0, data, len);
}

void strbuf_vinsertf(struct strbuf *sb, size_t pos, const char *fmt, va_list ap)
{
	int len, len2;
	char save;
	va_list cp;

	if (pos > sb->len)
		die("`pos' is too far after the end of the buffer");
	va_copy(cp, ap);
	len = vsnprintf(sb->buf + sb->len, 0, fmt, cp);
	va_end(cp);
	if (len < 0)
		BUG("your vsnprintf is broken (returned %d)", len);
	if (!len)
		return; /* nothing to do */
	if (unsigned_add_overflows(sb->len, len))
		die("you want to use way too much memory");
	strbuf_grow(sb, len);
	memmove(sb->buf + pos + len, sb->buf + pos, sb->len - pos);
	/* vsnprintf() will append a NUL, overwriting one of our characters */
	save = sb->buf[pos + len];
	len2 = vsnprintf(sb->buf + pos, len + 1, fmt, ap);
	sb->buf[pos + len] = save;
	if (len2 != len)
		BUG("your vsnprintf is broken (returns inconsistent lengths)");
	strbuf_setlen(sb, sb->len + len);
}

void strbuf_insertf(struct strbuf *sb, size_t pos, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	strbuf_vinsertf(sb, pos, fmt, ap);
	va_end(ap);
}

void strbuf_remove(struct strbuf *sb, size_t pos, size_t len)
{
	strbuf_splice(sb, pos, len, "", 0);
}

void strbuf_add(struct strbuf *sb, const void *data, size_t len)
{
	strbuf_grow(sb, len);
	memcpy(sb->buf + sb->len, data, len);
	strbuf_setlen(sb, sb->len + len);
}

void strbuf_addbuf(struct strbuf *sb, const struct strbuf *sb2)
{
	strbuf_grow(sb, sb2->len);
	memcpy(sb->buf + sb->len, sb2->buf, sb2->len);
	strbuf_setlen(sb, sb->len + sb2->len);
}

const char *strbuf_join_argv(struct strbuf *buf,
			     int argc, const char **argv, char delim)
{
	if (!argc)
		return buf->buf;

	strbuf_addstr(buf, *argv);
	while (--argc) {
		strbuf_addch(buf, delim);
		strbuf_addstr(buf, *(++argv));
	}

	return buf->buf;
}

void strbuf_addchars(struct strbuf *sb, int c, size_t n)
{
	strbuf_grow(sb, n);
	memset(sb->buf + sb->len, c, n);
	strbuf_setlen(sb, sb->len + n);
}

void strbuf_addf(struct strbuf *sb, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	strbuf_vaddf(sb, fmt, ap);
	va_end(ap);
}

static void add_lines(struct strbuf *out,
			const char *prefix1,
			const char *prefix2,
			const char *buf, size_t size)
{
	while (size) {
		const char *prefix;
		const char *next = memchr(buf, '\n', size);
		next = next ? (next + 1) : (buf + size);

		prefix = ((prefix2 && (buf[0] == '\n' || buf[0] == '\t'))
			  ? prefix2 : prefix1);
		strbuf_addstr(out, prefix);
		strbuf_add(out, buf, next - buf);
		size -= next - buf;
		buf = next;
	}
	strbuf_complete_line(out);
}

void strbuf_add_commented_lines(struct strbuf *out, const char *buf, size_t size)
{
	static char prefix1[3];
	static char prefix2[2];

	if (prefix1[0] != comment_line_char) {
		xsnprintf(prefix1, sizeof(prefix1), "%c ", comment_line_char);
		xsnprintf(prefix2, sizeof(prefix2), "%c", comment_line_char);
	}
	add_lines(out, prefix1, prefix2, buf, size);
}

void strbuf_commented_addf(struct strbuf *sb, const char *fmt, ...)
{
	va_list params;
	struct strbuf buf = STRBUF_INIT;
	int incomplete_line = sb->len && sb->buf[sb->len - 1] != '\n';

	va_start(params, fmt);
	strbuf_vaddf(&buf, fmt, params);
	va_end(params);

	strbuf_add_commented_lines(sb, buf.buf, buf.len);
	if (incomplete_line)
		sb->buf[--sb->len] = '\0';

	strbuf_release(&buf);
}

void strbuf_vaddf(struct strbuf *sb, const char *fmt, va_list ap)
{
	int len;
	va_list cp;

	if (!strbuf_avail(sb))
		strbuf_grow(sb, 64);
	va_copy(cp, ap);
	len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, cp);
	va_end(cp);
	if (len < 0)
		BUG("your vsnprintf is broken (returned %d)", len);
	if (len > strbuf_avail(sb)) {
		strbuf_grow(sb, len);
		len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, ap);
		if (len > strbuf_avail(sb))
			BUG("your vsnprintf is broken (insatiable)");
	}
	strbuf_setlen(sb, sb->len + len);
}

void strbuf_expand(struct strbuf *sb, const char *format, expand_fn_t fn,
		   void *context)
{
	for (;;) {
		const char *percent;
		size_t consumed;

		percent = strchrnul(format, '%');
		strbuf_add(sb, format, percent - format);
		if (!*percent)
			break;
		format = percent + 1;

		if (*format == '%') {
			strbuf_addch(sb, '%');
			format++;
			continue;
		}

		consumed = fn(sb, format, context);
		if (consumed)
			format += consumed;
		else
			strbuf_addch(sb, '%');
	}
}

size_t strbuf_expand_literal_cb(struct strbuf *sb,
				const char *placeholder,
				void *context UNUSED)
{
	int ch;

	switch (placeholder[0]) {
	case 'n':		/* newline */
		strbuf_addch(sb, '\n');
		return 1;
	case 'x':
		/* %x00 == NUL, %x0a == LF, etc. */
		ch = hex2chr(placeholder + 1);
		if (ch < 0)
			return 0;
		strbuf_addch(sb, ch);
		return 3;
	}
	return 0;
}

size_t strbuf_expand_dict_cb(struct strbuf *sb, const char *placeholder,
		void *context)
{
	struct strbuf_expand_dict_entry *e = context;
	size_t len;

	for (; e->placeholder && (len = strlen(e->placeholder)); e++) {
		if (!strncmp(placeholder, e->placeholder, len)) {
			if (e->value)
				strbuf_addstr(sb, e->value);
			return len;
		}
	}
	return 0;
}

void strbuf_addbuf_percentquote(struct strbuf *dst, const struct strbuf *src)
{
	size_t i, len = src->len;

	for (i = 0; i < len; i++) {
		if (src->buf[i] == '%')
			strbuf_addch(dst, '%');
		strbuf_addch(dst, src->buf[i]);
	}
}

#define URL_UNSAFE_CHARS " <>\"%{}|\\^`:?#[]@!$&'()*+,;="

void strbuf_add_percentencode(struct strbuf *dst, const char *src, int flags)
{
	size_t i, len = strlen(src);

	for (i = 0; i < len; i++) {
		unsigned char ch = src[i];
		if (ch <= 0x1F || ch >= 0x7F ||
		    (ch == '/' && (flags & STRBUF_ENCODE_SLASH)) ||
		    strchr(URL_UNSAFE_CHARS, ch))
			strbuf_addf(dst, "%%%02X", (unsigned char)ch);
		else
			strbuf_addch(dst, ch);
	}
}

size_t strbuf_fread(struct strbuf *sb, size_t size, FILE *f)
{
	size_t res;
	size_t oldalloc = sb->alloc;

	strbuf_grow(sb, size);
	res = fread(sb->buf + sb->len, 1, size, f);
	if (res > 0)
		strbuf_setlen(sb, sb->len + res);
	else if (oldalloc == 0)
		strbuf_release(sb);
	return res;
}

ssize_t strbuf_read(struct strbuf *sb, int fd, size_t hint)
{
	size_t oldlen = sb->len;
	size_t oldalloc = sb->alloc;

	strbuf_grow(sb, hint ? hint : 8192);
	for (;;) {
		ssize_t want = sb->alloc - sb->len - 1;
		ssize_t got = read_in_full(fd, sb->buf + sb->len, want);

		if (got < 0) {
			if (oldalloc == 0)
				strbuf_release(sb);
			else
				strbuf_setlen(sb, oldlen);
			return -1;
		}
		sb->len += got;
		if (got < want)
			break;
		strbuf_grow(sb, 8192);
	}

	sb->buf[sb->len] = '\0';
	return sb->len - oldlen;
}

ssize_t strbuf_read_once(struct strbuf *sb, int fd, size_t hint)
{
	size_t oldalloc = sb->alloc;
	ssize_t cnt;

	strbuf_grow(sb, hint ? hint : 8192);
	cnt = xread(fd, sb->buf + sb->len, sb->alloc - sb->len - 1);
	if (cnt > 0)
		strbuf_setlen(sb, sb->len + cnt);
	else if (oldalloc == 0)
		strbuf_release(sb);
	return cnt;
}

ssize_t strbuf_write(struct strbuf *sb, FILE *f)
{
	return sb->len ? fwrite(sb->buf, 1, sb->len, f) : 0;
}

#define STRBUF_MAXLINK (2*PATH_MAX)

int strbuf_readlink(struct strbuf *sb, const char *path, size_t hint)
{
	size_t oldalloc = sb->alloc;

	if (hint < 32)
		hint = 32;

	while (hint < STRBUF_MAXLINK) {
		ssize_t len;

		strbuf_grow(sb, hint);
		len = readlink(path, sb->buf, hint);
		if (len < 0) {
			if (errno != ERANGE)
				break;
		} else if (len < hint) {
			strbuf_setlen(sb, len);
			return 0;
		}

		/* .. the buffer was too small - try again */
		hint *= 2;
	}
	if (oldalloc == 0)
		strbuf_release(sb);
	return -1;
}

int strbuf_getcwd(struct strbuf *sb)
{
	size_t oldalloc = sb->alloc;
	size_t guessed_len = 128;

	for (;; guessed_len *= 2) {
		strbuf_grow(sb, guessed_len);
		if (getcwd(sb->buf, sb->alloc)) {
			strbuf_setlen(sb, strlen(sb->buf));
			return 0;
		}

		/*
		 * If getcwd(3) is implemented as a syscall that falls
		 * back to a regular lookup using readdir(3) etc. then
		 * we may be able to avoid EACCES by providing enough
		 * space to the syscall as it's not necessarily bound
		 * to the same restrictions as the fallback.
		 */
		if (errno == EACCES && guessed_len < PATH_MAX)
			continue;

		if (errno != ERANGE)
			break;
	}
	if (oldalloc == 0)
		strbuf_release(sb);
	else
		strbuf_reset(sb);
	return -1;
}

#ifdef HAVE_GETDELIM
int strbuf_getwholeline(struct strbuf *sb, FILE *fp, int term)
{
	ssize_t r;

	if (feof(fp))
		return EOF;

	strbuf_reset(sb);

	/* Translate slopbuf to NULL, as we cannot call realloc on it */
	if (!sb->alloc)
		sb->buf = NULL;
	errno = 0;
	r = getdelim(&sb->buf, &sb->alloc, term, fp);

	if (r > 0) {
		sb->len = r;
		return 0;
	}
	assert(r == -1);

	/*
	 * Normally we would have called xrealloc, which will try to free
	 * memory and recover. But we have no way to tell getdelim() to do so.
	 * Worse, we cannot try to recover ENOMEM ourselves, because we have
	 * no idea how many bytes were read by getdelim.
	 *
	 * Dying here is reasonable. It mirrors what xrealloc would do on
	 * catastrophic memory failure. We skip the opportunity to free pack
	 * memory and retry, but that's unlikely to help for a malloc small
	 * enough to hold a single line of input, anyway.
	 */
	if (errno == ENOMEM)
		die("Out of memory, getdelim failed");

	/*
	 * Restore strbuf invariants; if getdelim left us with a NULL pointer,
	 * we can just re-init, but otherwise we should make sure that our
	 * length is empty, and that the result is NUL-terminated.
	 */
	if (!sb->buf)
		strbuf_init(sb, 0);
	else
		strbuf_reset(sb);
	return EOF;
}
#else
int strbuf_getwholeline(struct strbuf *sb, FILE *fp, int term)
{
	int ch;

	if (feof(fp))
		return EOF;

	strbuf_reset(sb);
	flockfile(fp);
	while ((ch = getc_unlocked(fp)) != EOF) {
		if (!strbuf_avail(sb))
			strbuf_grow(sb, 1);
		sb->buf[sb->len++] = ch;
		if (ch == term)
			break;
	}
	funlockfile(fp);
	if (ch == EOF && sb->len == 0)
		return EOF;

	sb->buf[sb->len] = '\0';
	return 0;
}
#endif

int strbuf_appendwholeline(struct strbuf *sb, FILE *fp, int term)
{
	struct strbuf line = STRBUF_INIT;
	if (strbuf_getwholeline(&line, fp, term))
		return EOF;
	strbuf_addbuf(sb, &line);
	strbuf_release(&line);
	return 0;
}

static int strbuf_getdelim(struct strbuf *sb, FILE *fp, int term)
{
	if (strbuf_getwholeline(sb, fp, term))
		return EOF;
	if (sb->buf[sb->len - 1] == term)
		strbuf_setlen(sb, sb->len - 1);
	return 0;
}

int strbuf_getline(struct strbuf *sb, FILE *fp)
{
	if (strbuf_getwholeline(sb, fp, '\n'))
		return EOF;
	if (sb->buf[sb->len - 1] == '\n') {
		strbuf_setlen(sb, sb->len - 1);
		if (sb->len && sb->buf[sb->len - 1] == '\r')
			strbuf_setlen(sb, sb->len - 1);
	}
	return 0;
}

int strbuf_getline_lf(struct strbuf *sb, FILE *fp)
{
	return strbuf_getdelim(sb, fp, '\n');
}

int strbuf_getline_nul(struct strbuf *sb, FILE *fp)
{
	return strbuf_getdelim(sb, fp, '\0');
}

int strbuf_getwholeline_fd(struct strbuf *sb, int fd, int term)
{
	strbuf_reset(sb);

	while (1) {
		char ch;
		ssize_t len = xread(fd, &ch, 1);
		if (len <= 0)
			return EOF;
		strbuf_addch(sb, ch);
		if (ch == term)
			break;
	}
	return 0;
}

ssize_t strbuf_read_file(struct strbuf *sb, const char *path, size_t hint)
{
	int fd;
	ssize_t len;
	int saved_errno;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	len = strbuf_read(sb, fd, hint);
	saved_errno = errno;
	close(fd);
	if (len < 0) {
		errno = saved_errno;
		return -1;
	}

	return len;
}

void strbuf_add_lines(struct strbuf *out, const char *prefix,
		      const char *buf, size_t size)
{
	add_lines(out, prefix, NULL, buf, size);
}

void strbuf_addstr_xml_quoted(struct strbuf *buf, const char *s)
{
	while (*s) {
		size_t len = strcspn(s, "\"<>&");
		strbuf_add(buf, s, len);
		s += len;
		switch (*s) {
		case '"':
			strbuf_addstr(buf, "&quot;");
			break;
		case '<':
			strbuf_addstr(buf, "&lt;");
			break;
		case '>':
			strbuf_addstr(buf, "&gt;");
			break;
		case '&':
			strbuf_addstr(buf, "&amp;");
			break;
		case 0:
			return;
		}
		s++;
	}
}

int is_rfc3986_reserved_or_unreserved(char ch)
{
	if (is_rfc3986_unreserved(ch))
		return 1;
	switch (ch) {
		case '!': case '*': case '\'': case '(': case ')': case ';':
		case ':': case '@': case '&': case '=': case '+': case '$':
		case ',': case '/': case '?': case '#': case '[': case ']':
			return 1;
	}
	return 0;
}

int is_rfc3986_unreserved(char ch)
{
	return isalnum(ch) ||
		ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

static void strbuf_add_urlencode(struct strbuf *sb, const char *s, size_t len,
				 char_predicate allow_unencoded_fn)
{
	strbuf_grow(sb, len);
	while (len--) {
		char ch = *s++;
		if (allow_unencoded_fn(ch))
			strbuf_addch(sb, ch);
		else
			strbuf_addf(sb, "%%%02x", (unsigned char)ch);
	}
}

void strbuf_addstr_urlencode(struct strbuf *sb, const char *s,
			     char_predicate allow_unencoded_fn)
{
	strbuf_add_urlencode(sb, s, strlen(s), allow_unencoded_fn);
}

static void strbuf_humanise(struct strbuf *buf, off_t bytes,
				 int humanise_rate)
{
	if (bytes > 1 << 30) {
		strbuf_addf(buf,
				humanise_rate == 0 ?
					/* TRANSLATORS: IEC 80000-13:2008 gibibyte */
					_("%u.%2.2u GiB") :
					/* TRANSLATORS: IEC 80000-13:2008 gibibyte/second */
					_("%u.%2.2u GiB/s"),
			    (unsigned)(bytes >> 30),
			    (unsigned)(bytes & ((1 << 30) - 1)) / 10737419);
	} else if (bytes > 1 << 20) {
		unsigned x = bytes + 5243;  /* for rounding */
		strbuf_addf(buf,
				humanise_rate == 0 ?
					/* TRANSLATORS: IEC 80000-13:2008 mebibyte */
					_("%u.%2.2u MiB") :
					/* TRANSLATORS: IEC 80000-13:2008 mebibyte/second */
					_("%u.%2.2u MiB/s"),
			    x >> 20, ((x & ((1 << 20) - 1)) * 100) >> 20);
	} else if (bytes > 1 << 10) {
		unsigned x = bytes + 5;  /* for rounding */
		strbuf_addf(buf,
				humanise_rate == 0 ?
					/* TRANSLATORS: IEC 80000-13:2008 kibibyte */
					_("%u.%2.2u KiB") :
					/* TRANSLATORS: IEC 80000-13:2008 kibibyte/second */
					_("%u.%2.2u KiB/s"),
			    x >> 10, ((x & ((1 << 10) - 1)) * 100) >> 10);
	} else {
		strbuf_addf(buf,
				humanise_rate == 0 ?
					/* TRANSLATORS: IEC 80000-13:2008 byte */
					Q_("%u byte", "%u bytes", bytes) :
					/* TRANSLATORS: IEC 80000-13:2008 byte/second */
					Q_("%u byte/s", "%u bytes/s", bytes),
				(unsigned)bytes);
	}
}

void strbuf_humanise_bytes(struct strbuf *buf, off_t bytes)
{
	strbuf_humanise(buf, bytes, 0);
}

void strbuf_humanise_rate(struct strbuf *buf, off_t bytes)
{
	strbuf_humanise(buf, bytes, 1);
}

void strbuf_add_absolute_path(struct strbuf *sb, const char *path)
{
	if (!*path)
		die("The empty string is not a valid path");
	if (!is_absolute_path(path)) {
		struct stat cwd_stat, pwd_stat;
		size_t orig_len = sb->len;
		char *cwd = xgetcwd();
		char *pwd = getenv("PWD");
		if (pwd && strcmp(pwd, cwd) &&
		    !stat(cwd, &cwd_stat) &&
		    (cwd_stat.st_dev || cwd_stat.st_ino) &&
		    !stat(pwd, &pwd_stat) &&
		    pwd_stat.st_dev == cwd_stat.st_dev &&
		    pwd_stat.st_ino == cwd_stat.st_ino)
			strbuf_addstr(sb, pwd);
		else
			strbuf_addstr(sb, cwd);
		if (sb->len > orig_len && !is_dir_sep(sb->buf[sb->len - 1]))
			strbuf_addch(sb, '/');
		free(cwd);
	}
	strbuf_addstr(sb, path);
}

void strbuf_add_real_path(struct strbuf *sb, const char *path)
{
	if (sb->len) {
		struct strbuf resolved = STRBUF_INIT;
		strbuf_realpath(&resolved, path, 1);
		strbuf_addbuf(sb, &resolved);
		strbuf_release(&resolved);
	} else
		strbuf_realpath(sb, path, 1);
}

int printf_ln(const char *fmt, ...)
{
	int ret;
	va_list ap;
	va_start(ap, fmt);
	ret = vprintf(fmt, ap);
	va_end(ap);
	if (ret < 0 || putchar('\n') == EOF)
		return -1;
	return ret + 1;
}

int fprintf_ln(FILE *fp, const char *fmt, ...)
{
	int ret;
	va_list ap;
	va_start(ap, fmt);
	ret = vfprintf(fp, fmt, ap);
	va_end(ap);
	if (ret < 0 || putc('\n', fp) == EOF)
		return -1;
	return ret + 1;
}

char *xstrdup_tolower(const char *string)
{
	char *result;
	size_t len, i;

	len = strlen(string);
	result = xmallocz(len);
	for (i = 0; i < len; i++)
		result[i] = tolower(string[i]);
	return result;
}

char *xstrdup_toupper(const char *string)
{
	char *result;
	size_t len, i;

	len = strlen(string);
	result = xmallocz(len);
	for (i = 0; i < len; i++)
		result[i] = toupper(string[i]);
	return result;
}

char *xstrvfmt(const char *fmt, va_list ap)
{
	struct strbuf buf = STRBUF_INIT;
	strbuf_vaddf(&buf, fmt, ap);
	return strbuf_detach(&buf, NULL);
}

char *xstrfmt(const char *fmt, ...)
{
	va_list ap;
	char *ret;

	va_start(ap, fmt);
	ret = xstrvfmt(fmt, ap);
	va_end(ap);

	return ret;
}

void strbuf_addftime(struct strbuf *sb, const char *fmt, const struct tm *tm,
		     int tz_offset, int suppress_tz_name)
{
	struct strbuf munged_fmt = STRBUF_INIT;
	size_t hint = 128;
	size_t len;

	if (!*fmt)
		return;

	/*
	 * There is no portable way to pass timezone information to
	 * strftime, so we handle %z and %Z here. Likewise '%s', because
	 * going back to an epoch time requires knowing the zone.
	 *
	 * Note that tz_offset is in the "[-+]HHMM" decimal form; this is what
	 * we want for %z, but the computation for %s has to convert to number
	 * of seconds.
	 */
	for (;;) {
		const char *percent = strchrnul(fmt, '%');
		strbuf_add(&munged_fmt, fmt, percent - fmt);
		if (!*percent)
			break;
		fmt = percent + 1;
		switch (*fmt) {
		case '%':
			strbuf_addstr(&munged_fmt, "%%");
			fmt++;
			break;
		case 's':
			strbuf_addf(&munged_fmt, "%"PRItime,
				    (timestamp_t)tm_to_time_t(tm) -
				    3600 * (tz_offset / 100) -
				    60 * (tz_offset % 100));
			fmt++;
			break;
		case 'z':
			strbuf_addf(&munged_fmt, "%+05d", tz_offset);
			fmt++;
			break;
		case 'Z':
			if (suppress_tz_name) {
				fmt++;
				break;
			}
			/* FALLTHROUGH */
		default:
			strbuf_addch(&munged_fmt, '%');
		}
	}
	fmt = munged_fmt.buf;

	strbuf_grow(sb, hint);
	len = strftime(sb->buf + sb->len, sb->alloc - sb->len, fmt, tm);

	if (!len) {
		/*
		 * strftime reports "0" if it could not fit the result in the buffer.
		 * Unfortunately, it also reports "0" if the requested time string
		 * takes 0 bytes. So our strategy is to munge the format so that the
		 * output contains at least one character, and then drop the extra
		 * character before returning.
		 */
		strbuf_addch(&munged_fmt, ' ');
		while (!len) {
			hint *= 2;
			strbuf_grow(sb, hint);
			len = strftime(sb->buf + sb->len, sb->alloc - sb->len,
				       munged_fmt.buf, tm);
		}
		len--; /* drop munged space */
	}
	strbuf_release(&munged_fmt);
	strbuf_setlen(sb, sb->len + len);
}

void strbuf_repo_add_unique_abbrev(struct strbuf *sb, struct repository *repo,
				   const struct object_id *oid, int abbrev_len)
{
	int r;
	strbuf_grow(sb, GIT_MAX_HEXSZ + 1);
	r = repo_find_unique_abbrev_r(repo, sb->buf + sb->len, oid, abbrev_len);
	strbuf_setlen(sb, sb->len + r);
}

void strbuf_add_unique_abbrev(struct strbuf *sb, const struct object_id *oid,
			      int abbrev_len)
{
	strbuf_repo_add_unique_abbrev(sb, the_repository, oid, abbrev_len);
}

/*
 * Returns the length of a line, without trailing spaces.
 *
 * If the line ends with newline, it will be removed too.
 */
static size_t cleanup(char *line, size_t len)
{
	while (len) {
		unsigned char c = line[len - 1];
		if (!isspace(c))
			break;
		len--;
	}

	return len;
}

/*
 * Remove empty lines from the beginning and end
 * and also trailing spaces from every line.
 *
 * Turn multiple consecutive empty lines between paragraphs
 * into just one empty line.
 *
 * If the input has only empty lines and spaces,
 * no output will be produced.
 *
 * If last line does not have a newline at the end, one is added.
 *
 * Enable skip_comments to skip every line starting with comment
 * character.
 */
void strbuf_stripspace(struct strbuf *sb, int skip_comments)
{
	size_t empties = 0;
	size_t i, j, len, newlen;
	char *eol;

	/* We may have to add a newline. */
	strbuf_grow(sb, 1);

	for (i = j = 0; i < sb->len; i += len, j += newlen) {
		eol = memchr(sb->buf + i, '\n', sb->len - i);
		len = eol ? eol - (sb->buf + i) + 1 : sb->len - i;

		if (skip_comments && len && sb->buf[i] == comment_line_char) {
			newlen = 0;
			continue;
		}
		newlen = cleanup(sb->buf + i, len);

		/* Not just an empty line? */
		if (newlen) {
			if (empties > 0 && j > 0)
				sb->buf[j++] = '\n';
			empties = 0;
			memmove(sb->buf + j, sb->buf + i, newlen);
			sb->buf[newlen + j++] = '\n';
		} else {
			empties++;
		}
	}

	strbuf_setlen(sb, j);
}

int strbuf_normalize_path(struct strbuf *src)
{
	struct strbuf dst = STRBUF_INIT;

	strbuf_grow(&dst, src->len);
	if (normalize_path_copy(dst.buf, src->buf) < 0) {
		strbuf_release(&dst);
		return -1;
	}

	/*
	 * normalize_path does not tell us the new length, so we have to
	 * compute it by looking for the new NUL it placed
	 */
	strbuf_setlen(&dst, strlen(dst.buf));
	strbuf_swap(src, &dst);
	strbuf_release(&dst);
	return 0;
}

void strbuf_strip_file_from_path(struct strbuf *sb)
{
	char *path_sep = find_last_dir_sep(sb->buf);
	strbuf_setlen(sb, path_sep ? path_sep - sb->buf + 1 : 0);
}
