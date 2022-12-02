#include "test-tool.h"
#include "cache.h"

/*
 * Usage: test-tool crontab <file> -l|<input>
 *
 * If -l is specified, then write the contents of <file> to stdout.
 * Otherwise, copy the contents of <input> into <file>.
 */
int cmd__crontab(int argc, const char **argv)
{
	int a;
	FILE *from, *to;

	if (argc != 3)
		usage("test-tool crontab <file> -l|<input>");

	if (!strcmp(argv[2], "-l")) {
		from = fopen(argv[1], "r");
		if (!from)
			return 0;
		to = stdout;
	} else {
		from = xfopen(argv[2], "r");
		to = xfopen(argv[1], "w");
	}

	while ((a = fgetc(from)) != EOF)
		fputc(a, to);

	fclose(from);
	if (to != stdout)
		fclose(to);

	return 0;
}
