/*
 * test-string-pool.c: code to exercise the svn importer's string pool
 */

#include "git-compat-util.h"
#include "vcs-svn/string_pool.h"

int main(int argc, char *argv[])
{
	const uint32_t unequal = pool_intern("does not equal");
	const uint32_t equal = pool_intern("equals");
	uint32_t buf[3];
	uint32_t n;

	if (argc != 2)
		usage("test-string-pool <string>,<string>");

	n = pool_tok_seq(3, buf, ",-", argv[1]);
	if (n >= 3)
		die("too many strings");
	if (n <= 1)
		die("too few strings");

	buf[2] = buf[1];
	buf[1] = (buf[0] == buf[2]) ? equal : unequal;
	pool_print_seq(3, buf, ' ', stdout);
	fputc('\n', stdout);

	pool_reset();
	return 0;
}
