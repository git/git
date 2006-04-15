#include <stdio.h>
#include <string.h>
#include <ctype.h>

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

int main(int argc, char **argv)
{
	int empties = -1;
	int incomplete = 0;
	char line[1024];

	while (fgets(line, sizeof(line), stdin)) {
		incomplete = cleanup(line);

		/* Not just an empty line? */
		if (line[0] != '\n') {
			if (empties > 0)
				putchar('\n');
			empties = 0;
			fputs(line, stdout);
			continue;
		}
		if (empties < 0)
			continue;
		empties++;
	}
	if (incomplete)
		putchar('\n');
	return 0;
}
