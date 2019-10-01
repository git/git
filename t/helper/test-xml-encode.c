#include "test-tool.h"

static const char *utf8_replace_character = "&#xfffd;";

/*
 * Encodes (possibly incorrect) UTF-8 on <stdin> to <stdout>, to be embedded
 * in an XML file.
 */
int cmd__xml_encode(int argc, const char **argv)
{
	unsigned char buf[1024], tmp[4], *tmp2 = NULL;
	ssize_t cur = 0, len = 1, remaining = 0;
	unsigned char ch;

	for (;;) {
		if (++cur == len) {
			len = xread(0, buf, sizeof(buf));
			if (!len)
				return 0;
			if (len < 0)
				die_errno("Could not read <stdin>");
			cur = 0;
		}
		ch = buf[cur];

		if (tmp2) {
			if ((ch & 0xc0) != 0x80) {
				fputs(utf8_replace_character, stdout);
				tmp2 = NULL;
				cur--;
				continue;
			}
			*tmp2 = ch;
			tmp2++;
			if (--remaining == 0) {
				fwrite(tmp, tmp2 - tmp, 1, stdout);
				tmp2 = NULL;
			}
			continue;
		}

		if (!(ch & 0x80)) {
			/* 0xxxxxxx */
			if (ch == '&')
				fputs("&amp;", stdout);
			else if (ch == '\'')
				fputs("&apos;", stdout);
			else if (ch == '"')
				fputs("&quot;", stdout);
			else if (ch == '<')
				fputs("&lt;", stdout);
			else if (ch == '>')
				fputs("&gt;", stdout);
			else if (ch >= 0x20)
				fputc(ch, stdout);
			else if (ch == 0x09 || ch == 0x0a || ch == 0x0d)
				fprintf(stdout, "&#x%02x;", ch);
			else
				fputs(utf8_replace_character, stdout);
		} else if ((ch & 0xe0) == 0xc0) {
			/* 110XXXXx 10xxxxxx */
			tmp[0] = ch;
			remaining = 1;
			tmp2 = tmp + 1;
		} else if ((ch & 0xf0) == 0xe0) {
			/* 1110XXXX 10Xxxxxx 10xxxxxx */
			tmp[0] = ch;
			remaining = 2;
			tmp2 = tmp + 1;
		} else if ((ch & 0xf8) == 0xf0) {
			/* 11110XXX 10XXxxxx 10xxxxxx 10xxxxxx */
			tmp[0] = ch;
			remaining = 3;
			tmp2 = tmp + 1;
		} else
			fputs(utf8_replace_character, stdout);
	}

	return 0;
}
