/*
 * test-mktemp.c: code to exercise the creation of temporary files
 */
#include "git-compat-util.h"

int cmd_main(int argc, const char **argv)
{
	if (argc != 2)
		usage("Expected 1 parameter defining the temporary file template");

	xmkstemp(xstrdup(argv[1]));

	return 0;
}
