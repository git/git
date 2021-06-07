#include "test-tool.h"
#include "cache.h"

int cmd__read_cache_perf(int argc, const char **argv)
{
	struct repository *r = the_repository;
	int i, cnt = 1;

	if (argc == 2)
		cnt = strtol(argv[1], NULL, 0);
	else
		die("usage: test-tool read-cache-perf [<count>]");

	setup_git_directory();
	for (i = 0; i < cnt; i++) {
		repo_read_index(r);
		discard_index(r->index);
	}

	return 0;
}
