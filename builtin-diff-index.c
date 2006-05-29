#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "revision.h"
#include "builtin.h"

static const char diff_cache_usage[] =
"git-diff-index [-m] [--cached] "
"[<common diff options>] <tree-ish> [<path>...]"
COMMON_DIFF_OPTIONS_HELP;

int cmd_diff_index(int argc, const char **argv, char **envp)
{
	struct rev_info rev;
	int cached = 0;
	int i;

	git_config(git_diff_config);
	init_revisions(&rev);
	rev.abbrev = 0;

	argc = setup_revisions(argc, argv, &rev, NULL);
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
			
		if (!strcmp(arg, "--cached"))
			cached = 1;
		else
			usage(diff_cache_usage);
	}
	/*
	 * Make sure there is one revision (i.e. pending object),
	 * and there is no revision filtering parameters.
	 */
	if (!rev.pending_objects || rev.pending_objects->next ||
	    rev.max_count != -1 || rev.min_age != -1 || rev.max_age != -1)
		usage(diff_cache_usage);
	return run_diff_index(&rev, cached);
}
