#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "cache.h"
#include "config.h"
#include "diff.h"
#include "diff-merges.h"
#include "commit.h"
#include "revision.h"
#include "builtin.h"
#include "submodule.h"

static const char diff_cache_usage[] =
"git diff-index [-m] [--cached] [--merge-base] "
"[<common-diff-options>] <tree-ish> [<path>...]"
"\n"
COMMON_DIFF_OPTIONS_HELP;

int cmd_diff_index(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	unsigned int option = 0;
	int i;
	int result;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(diff_cache_usage);

	git_config(git_diff_basic_config, NULL); /* no "diff" UI options */
	repo_init_revisions(the_repository, &rev, prefix);
	rev.abbrev = 0;
	prefix = precompose_argv_prefix(argc, argv, prefix);

	/*
	 * We need (some of) diff for merges options (e.g., --cc), and we need
	 * to avoid conflict with our own meaning of "-m".
	 */
	diff_merges_suppress_m_parsing();

	argc = setup_revisions(argc, argv, &rev, NULL);
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "--cached"))
			option |= DIFF_INDEX_CACHED;
		else if (!strcmp(arg, "--merge-base"))
			option |= DIFF_INDEX_MERGE_BASE;
		else if (!strcmp(arg, "-m"))
			rev.match_missing = 1;
		else
			usage(diff_cache_usage);
	}
	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_RAW;

	rev.diffopt.rotate_to_strict = 1;

	/*
	 * Make sure there is one revision (i.e. pending object),
	 * and there is no revision filtering parameters.
	 */
	if (rev.pending.nr != 1 ||
	    rev.max_count != -1 || rev.min_age != -1 || rev.max_age != -1)
		usage(diff_cache_usage);
	if (!(option & DIFF_INDEX_CACHED)) {
		setup_work_tree();
		if (read_cache_preload(&rev.diffopt.pathspec) < 0) {
			perror("read_cache_preload");
			return -1;
		}
	} else if (read_cache() < 0) {
		perror("read_cache");
		return -1;
	}
	result = run_diff_index(&rev, option);
	result = diff_result_code(&rev.diffopt, result);
	release_revisions(&rev);
	return result;
}
