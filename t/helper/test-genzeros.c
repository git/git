#define DISABLE_SIGN_COMPARE_WARNINGS

#include "test-tool.h"
#include "git-compat-util.h"
#include "parse.h"

/*
 * Write "count" copies of "byte" to stdout, or an endless stream of them if
 * "count" is negative.
 */
static int generate_bytes(unsigned char byte, intmax_t count)
{
	static char buf[256 * 1024];
	ssize_t n;

	memset(buf, byte, sizeof(buf));

	/* Writing out individual bytes is slow... */
	while (count < 0)
		if (xwrite(1, buf, ARRAY_SIZE(buf)) < 0)
			die_errno("write error");

	while (count > 0) {
		n = xwrite(1, buf,
			   count < ARRAY_SIZE(buf)
			   ? count : ARRAY_SIZE(buf));

		if (n < 0)
			die_errno("write error");

		count -= n;
	}

	return 0;
}

int cmd__genbytes(int argc, const char **argv)
{
	intmax_t count = -1;

	if (argc < 2 || argc > 3 || strlen(argv[1]) != 1 ||
	    (argc == 3 && !git_parse_signed(argv[2], &count, INTMAX_MAX))) {
		fprintf(stderr, "usage: %s <char> [<count>]\n", argv[0]);
		return 1;
	}

	return generate_bytes(argv[1][0], count);
}

int cmd__genzeros(int argc, const char **argv)
{
	intmax_t count = -1;

	if (argc > 2 ||
	    (argc == 2 && !git_parse_signed(argv[1], &count, INTMAX_MAX))) {
		fprintf(stderr, "usage: %s [<count>]\n", argv[0]);
		return 1;
	}

	return generate_bytes('\0', count);
}
