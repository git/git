#include "test-tool.h"
#include "cache.h"

/*
 * Usage: test-tool cron <file> [-l]
 *
 * If -l is specified, then write the contents of <file> to stdout.
 * Otherwise, write from stdin into <file>.
 */
int cmd__crontab(int argc, const char **argv)
{
	int a;
	FILE *from, *to;

	if (argc == 3 && !strcmp(argv[2], "-l")) {
		from = fopen(argv[1], "r");
		if (!from)
			return 0;
		to = stdout;
	} else if (argc == 2) {
		from = stdin;
		to = fopen(argv[1], "w");
	} else
		return error("unknown arguments");

	while ((a = fgetc(from)) != EOF)
		fputc(a, to);

	if (argc == 3)
		fclose(from);
	else
		fclose(to);

	return 0;
}
