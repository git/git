#include "diff.h"
#include "log-tree.h"
#include "color.h"
#include "format-support.h"

static int istitlechar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '.' || c == '_';
}

void format_sanitized_subject(struct strbuf *sb, const char *msg, size_t len)
{
	char *r = xmemdupz(msg, len);
	size_t trimlen;
	size_t start_len = sb->len;
	int space = 2;
	int i;

	for (i = 0; i < len; i++) {
		if (r[i] == '\n')
			r[i] = ' ';
		if (istitlechar(r[i])) {
			if (space == 1)
				strbuf_addch(sb, '-');
			space = 0;
			strbuf_addch(sb, r[i]);
			if (r[i] == '.')
				while (r[i+1] == '.')
					i++;
		} else
			space |= 1;
	}
	free(r);

	/* trim any trailing '.' or '-' characters */
	trimlen = 0;
	while (sb->len - trimlen > start_len &&
		(sb->buf[sb->len - 1 - trimlen] == '.'
		|| sb->buf[sb->len - 1 - trimlen] == '-'))
		trimlen++;
	strbuf_remove(sb, sb->len - trimlen, trimlen);
}
