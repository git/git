#include <stdio.h>
#include <string.h>

int cmd_main(int argc, const char **argv)
{
	if (argc == 2 && strcmp(argv[1], "(size_t)(-20)") == 0)
		printf("%zu", (ssize_t)(-20));

	return 0;
}
