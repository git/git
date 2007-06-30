#include "builtin.h"
#include "cache.h"

/*
 * Remove trailing spaces from a line.
 *
 * If the line ends with newline, it will be removed too.
 * Returns the new length of the string.
 */
static int cleanup(char *line, int len)
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
		line[len] = 0;
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
 * Enable skip_comments to skip every line starting with "#".
 */
void stripspace(FILE *in, FILE *out, int skip_comments)
{
	int empties = -1;
	int alloc = 1024;
	char *line = xmalloc(alloc);

	while (fgets(line, alloc, in)) {
		int len = strlen(line);

		while (len == alloc - 1 && line[len - 1] != '\n') {
			alloc = alloc_nr(alloc);
			line = xrealloc(line, alloc);
			fgets(line + len, alloc - len, in);
			len += strlen(line + len);
		}

		if (skip_comments && line[0] == '#')
			continue;
		len = cleanup(line, len);

		/* Not just an empty line? */
		if (len) {
			if (empties > 0)
				fputc('\n', out);
			empties = 0;
			fputs(line, out);
			fputc('\n', out);
			continue;
		}
		if (empties < 0)
			continue;
		empties++;
	}
	free(line);
}

int cmd_stripspace(int argc, const char **argv, const char *prefix)
{
	stripspace(stdin, stdout, 0);
	return 0;
}
