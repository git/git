#include "test-tool.h"
#include "git-compat-util.h"

int cmd__genzeros(int argc, const char **argv)
{
	intmax_t count;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [<count>]\n", argv[0]);
		return 1;
	}

	count = argc > 1 ? strtoimax(argv[1], NULL, 0) : -1;

	while (count < 0 || count--) {
		if (putchar(0) == EOF)
			return -1;
	}

	return 0;
}
