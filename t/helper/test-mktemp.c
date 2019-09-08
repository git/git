/*
 * test-mktemp.c: code to exercise the creation of temporary files
 */
#include "test-tool.h"
#include "git-compat-util.h"

int cmd__mktemp(int argc, const char **argv)
{
	if (argc != 2)
		usage("Expected 1 parameter defining the temporary file template");

	xmkstemp(xstrdup(argv[1]));

	return 0;
}
