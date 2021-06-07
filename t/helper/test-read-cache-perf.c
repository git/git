#include "test-tool.h"
#include "cache.h"
#include "parse-options.h"

static const char *read_cache_perf_usage[] = {
	"test-tool read-cache-perf [<options>...]",
	NULL
};

int cmd__read_cache_perf(int argc, const char **argv)
{
	struct repository *r = the_repository;
	int cnt = -1;
	struct option options[] = {
		OPT_INTEGER(0, "count", &cnt, "number of passes"),
		OPT_END()
	};

	argc = parse_options(argc, argv, "test-tools", options,
			     read_cache_perf_usage, 0);
	if (argc > 0)
		usage_msg_opt("Too many arguments.", read_cache_perf_usage,
			      options);
	if (cnt < 1)
		usage_msg_opt("Need at least one pass.", read_cache_perf_usage,
			      options);

	setup_git_directory();
	while (cnt--) {
		repo_read_index(r);
		discard_index(r->index);
	}

	return 0;
}
