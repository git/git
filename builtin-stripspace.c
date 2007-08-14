#include "builtin.h"
#include "cache.h"

/*
 * Returns the length of a line, without trailing spaces.
 *
 * If the line ends with newline, it will be removed too.
 */
static size_t cleanup(char *line, size_t len)
{
	if (len) {
		if (line[len - 1] == '\n')
			len--;

		while (len) {
			unsigned char c = line[len - 1];
			if (!isspace(c))
				break;
			len--;
		}
	}
	return len;
}

/*
 * Remove empty lines from the beginning and end
 * and also trailing spaces from every line.
 *
 * Note that the buffer will not be NUL-terminated.
 *
 * Turn multiple consecutive empty lines between paragraphs
 * into just one empty line.
 *
 * If the input has only empty lines and spaces,
 * no output will be produced.
 *
 * If last line has a newline at the end, it will be removed.
 *
 * Enable skip_comments to skip every line starting with "#".
 */
size_t stripspace(char *buffer, size_t length, int skip_comments)
{
	int empties = -1;
	size_t i, j, len, newlen;
	char *eol;

	for (i = j = 0; i < length; i += len, j += newlen) {
		eol = memchr(buffer + i, '\n', length - i);
		len = eol ? eol - (buffer + i) + 1 : length - i;

		if (skip_comments && len && buffer[i] == '#') {
			newlen = 0;
			continue;
		}
		newlen = cleanup(buffer + i, len);

		/* Not just an empty line? */
		if (newlen) {
			if (empties != -1)
				buffer[j++] = '\n';
			if (empties > 0)
				buffer[j++] = '\n';
			empties = 0;
			memmove(buffer + j, buffer + i, newlen);
			continue;
		}
		if (empties < 0)
			continue;
		empties++;
	}

	return j;
}

int cmd_stripspace(int argc, const char **argv, const char *prefix)
{
	char *buffer;
	unsigned long size;
	int strip_comments = 0;

	if (argc > 1 && (!strcmp(argv[1], "-s") ||
				!strcmp(argv[1], "--strip-comments")))
		strip_comments = 1;

	size = 1024;
	buffer = xmalloc(size);
	if (read_fd(0, &buffer, &size)) {
		free(buffer);
		die("could not read the input");
	}

	size = stripspace(buffer, size, strip_comments);
	write_or_die(1, buffer, size);
	if (size)
		putc('\n', stdout);

	free(buffer);
	return 0;
}
