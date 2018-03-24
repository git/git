#include "test-tool.h"
#include "cache.h"

int cmd__read_cache(int argc, const char **argv)
{
	int i, cnt = 1;
	if (argc == 2)
		cnt = strtol(argv[1], NULL, 0);
	setup_git_directory();
	for (i = 0; i < cnt; i++) {
		read_cache();
		discard_cache();
	}
	return 0;
}
