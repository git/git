#include "cache.h"
#include "strbuf.h"

void strbuf_init(struct strbuf *sb) {
	memset(sb, 0, sizeof(*sb));
}

void strbuf_release(struct strbuf *sb) {
	free(sb->buf);
	memset(sb, 0, sizeof(*sb));
}

void strbuf_reset(struct strbuf *sb) {
	if (sb->len)
		strbuf_setlen(sb, 0);
	sb->eof = 0;
}

char *strbuf_detach(struct strbuf *sb) {
	char *res = sb->buf;
	strbuf_init(sb);
	return res;
}

void strbuf_grow(struct strbuf *sb, size_t extra) {
	if (sb->len + extra + 1 <= sb->len)
		die("you want to use way too much memory");
	ALLOC_GROW(sb->buf, sb->len + extra + 1, sb->alloc);
}

void strbuf_add(struct strbuf *sb, const void *data, size_t len) {
	strbuf_grow(sb, len);
	memcpy(sb->buf + sb->len, data, len);
	strbuf_setlen(sb, sb->len + len);
}

void strbuf_addf(struct strbuf *sb, const char *fmt, ...) {
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, ap);
	va_end(ap);
	if (len < 0) {
		len = 0;
	}
	if (len >= strbuf_avail(sb)) {
		strbuf_grow(sb, len);
		va_start(ap, fmt);
		len = vsnprintf(sb->buf + sb->len, sb->alloc - sb->len, fmt, ap);
		va_end(ap);
		if (len >= strbuf_avail(sb)) {
			die("this should not happen, your snprintf is broken");
		}
	}
	strbuf_setlen(sb, sb->len + len);
}

size_t strbuf_fread(struct strbuf *sb, size_t size, FILE *f) {
	size_t res;

	strbuf_grow(sb, size);
	res = fread(sb->buf + sb->len, 1, size, f);
	if (res > 0) {
		strbuf_setlen(sb, sb->len + res);
	}
	return res;
}

ssize_t strbuf_read(struct strbuf *sb, int fd)
{
	size_t oldlen = sb->len;

	for (;;) {
		ssize_t cnt;

		strbuf_grow(sb, 8192);
		cnt = xread(fd, sb->buf + sb->len, sb->alloc - sb->len - 1);
		if (cnt < 0) {
			strbuf_setlen(sb, oldlen);
			return -1;
		}
		if (!cnt)
			break;
		sb->len += cnt;
	}

	sb->buf[sb->len] = '\0';
	return sb->len - oldlen;
}

void read_line(struct strbuf *sb, FILE *fp, int term) {
	int ch;
	if (feof(fp)) {
		strbuf_release(sb);
		sb->eof = 1;
		return;
	}

	strbuf_reset(sb);
	while ((ch = fgetc(fp)) != EOF) {
		if (ch == term)
			break;
		strbuf_grow(sb, 1);
		sb->buf[sb->len++] = ch;
	}
	if (ch == EOF && sb->len == 0) {
		strbuf_release(sb);
		sb->eof = 1;
	}

	strbuf_grow(sb, 1);
	sb->buf[sb->len] = '\0';
}
