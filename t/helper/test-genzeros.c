#include "test-tool.h"
#include "git-compat-util.h"

int cmd__genzeros(int argc, const char **argv)
{
	/* static, so that it is NUL-initialized */
	static const char zeros[256 * 1024];
	intmax_t count;
	ssize_t n;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [<count>]\n", argv[0]);
		return 1;
	}

	count = argc > 1 ? strtoimax(argv[1], NULL, 0) : -1;

	/* Writing out individual NUL bytes is slow... */
	while (count < 0)
		if (write(1, zeros, ARRAY_SIZE(zeros)) < 0)
			return -1;

	while (count > 0) {
		n = write(1, zeros, count < ARRAY_SIZE(zeros) ?
			  count : ARRAY_SIZE(zeros));

		if (n < 0)
			return -1;

		count -= n;
	}

	return 0;
}
