#include "test-tool.h"
#include "cache.h"
#include "parse-options.h"

static const char *read_cache_again_usage[] = {
	"test-tool read-cache-again [<options>...] <file>",
	NULL
};

int cmd__read_cache_again(int argc, const char **argv)
{
	struct repository *r = the_repository;
	int cnt = -1;
	const char *name;
	struct option options[] = {
		OPT_INTEGER(0, "count", &cnt, "number of passes"),
		OPT_END()
	};

	argc = parse_options(argc, argv, "test-tools", options,
			     read_cache_again_usage, 0);
	if (argc != 1)
		usage_msg_opt("Too many arguments.", read_cache_again_usage,
			      options);
	if (cnt == -1)
		cnt = 2;
	else if (cnt < 1)
		usage_msg_opt("Need at least one pass.", read_cache_again_usage,
			      options);
	name = argv[2];

	setup_git_directory();
	while (cnt--) {
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
