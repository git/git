#include <stdio.h>
#include <string.h>

#include "selftest.h"

const char *selftest_binary_path;

#ifdef _WIN32
int __cdecl main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <selftest-suite-executable> <options>\n",
			argv[0]);
		exit(1);
	}

	selftest_binary_path = argv[1];
	memmove(argv + 1, argv + 2, argc - 1);
	argc -= 1;

	return clar_test(argc, argv);
}
