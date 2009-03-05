#include "cache.h"

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "normalize_path_copy")) {
		char *buf = xmalloc(PATH_MAX + 1);
		int rv = normalize_path_copy(buf, argv[2]);
		if (rv)
			buf = "++failed++";
		puts(buf);
		return 0;
	}

	if (argc >= 2 && !strcmp(argv[1], "make_absolute_path")) {
		while (argc > 2) {
			puts(make_absolute_path(argv[2]));
			argc--;
			argv++;
		}
		return 0;
	}

	if (argc == 4 && !strcmp(argv[1], "longest_ancestor_length")) {
		int len = longest_ancestor_length(argv[2], argv[3]);
		printf("%d\n", len);
		return 0;
	}

	if (argc == 4 && !strcmp(argv[1], "strip_path_suffix")) {
		char *prefix = strip_path_suffix(argv[2], argv[3]);
		printf("%s\n", prefix ? prefix : "(null)");
		return 0;
	}

	fprintf(stderr, "%s: unknown function name: %s\n", argv[0],
		argv[1] ? argv[1] : "(there was none)");
	return 1;
}
