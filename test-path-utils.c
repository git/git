#include "cache.h"

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "normalize_absolute_path")) {
		char *buf = xmalloc(strlen(argv[2])+1);
		int rv = normalize_absolute_path(buf, argv[2]);
		assert(strlen(buf) == rv);
		puts(buf);
	}

	return 0;
}
