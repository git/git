#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "builtin.h"

/*
 * Remove empty lines from the beginning and end.
 *
 * Turn multiple consecutive empty lines into just one
 * empty line.  Return true if it is an incomplete line.
 */
static int cleanup(char *line)
{
	int len = strlen(line);

	if (len && line[len-1] == '\n') {
		if (len == 1)
			return 0;
		do {
			unsigned char c = line[len-2];
			if (!isspace(c))
				break;
			line[len-2] = '\n';
			len--;
			line[len] = 0;
		} while (len > 1);
		return 0;
	}
	return 1;
}

void stripspace(FILE *in, FILE *out)
{
	int empties = -1;
	int incomplete = 0;
	char line[1024];

	while (fgets(line, sizeof(line), in)) {
		incomplete = cleanup(line);

		/* Not just an empty line? */
		if (line[0] != '\n') {
			if (empties > 0)
				fputc('\n', out);
			empties = 0;
			fputs(line, out);
			continue;
		}
		if (empties < 0)
			continue;
		empties++;
	}
	if (incomplete)
		fputc('\n', out);
}

int cmd_stripspace(int argc, const char **argv, char **envp)
{
	stripspace(stdin, stdout);
	return 0;
}
