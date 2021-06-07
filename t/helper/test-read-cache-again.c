#include "test-tool.h"
#include "cache.h"

int cmd__read_cache_again(int argc, const char **argv)
{
	struct repository *r = the_repository;
	int i, cnt;
	const char *name;

	if (argc != 2)
		die("usage: test-tool read-cache-again <count> <file>");

	cnt = strtol(argv[0], NULL, 0);
	name = argv[2];

	setup_git_directory();
	for (i = 0; i < cnt; i++) {
		int pos;
		repo_read_index(r);
		refresh_index(r->index, REFRESH_QUIET,
			      NULL, NULL, NULL);
		pos = index_name_pos(r->index, name, strlen(name));
		if (pos < 0)
			die("%s not in index", name);
		printf("%s is%s up to date\n", name,
		       ce_uptodate(r->index->cache[pos]) ? "" : " not");
		write_file(name, "%d\n", cnt);
		discard_index(r->index);
	}
	return 0;
}
