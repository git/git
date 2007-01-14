#include "cache.h"
#include "strbuf.h"

void strbuf_init(struct strbuf *sb) {
	sb->buf = NULL;
	sb->eof = sb->alloc = sb->len = 0;
}

static void strbuf_begin(struct strbuf *sb) {
	free(sb->buf);
	strbuf_init(sb);
}

static void inline strbuf_add(struct strbuf *sb, int ch) {
	if (sb->alloc <= sb->len) {
		sb->alloc = sb->alloc * 3 / 2 + 16;
		sb->buf = xrealloc(sb->buf, sb->alloc);
	}
	sb->buf[sb->len++] = ch;
}

static void strbuf_end(struct strbuf *sb) {
	strbuf_add(sb, 0);
}

void read_line(struct strbuf *sb, FILE *fp, int term) {
	int ch;
	strbuf_begin(sb);
	if (feof(fp)) {
		sb->eof = 1;
		return;
	}
	while ((ch = fgetc(fp)) != EOF) {
		if (ch == term)
			break;
		strbuf_add(sb, ch);
	}
	if (ch == EOF && sb->len == 0)
		sb->eof = 1;
	strbuf_end(sb);
}

