#include "cache.h"

int main(int argc, char **argv)
{
	while (argc > 1) {
		puts(make_absolute_path(argv[1]));
		argc--;
		argv++;
	}
	return 0;
}
