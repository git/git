/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "revision.h"
#include "builtin.h"

static const char diff_files_usage[] =
"git-diff-files [-q] [-0/-1/2/3 |-c|--cc] [<common diff options>] [<path>...]"
COMMON_DIFF_OPTIONS_HELP;

int cmd_diff_files(int argc, const char **argv, char **envp)
{
	struct rev_info rev;
	int silent = 0;

	git_config(git_default_config); /* no "diff" UI options */
	init_revisions(&rev);
	rev.abbrev = 0;

	argc = setup_revisions(argc, argv, &rev, NULL);
	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "--base"))
			rev.max_count = 1;
		else if (!strcmp(argv[1], "--ours"))
			rev.max_count = 2;
		else if (!strcmp(argv[1], "--theirs"))
			rev.max_count = 3;
		else if (!strcmp(argv[1], "-q"))
			silent = 1;
		else
			usage(diff_files_usage);
		argv++; argc--;
	}
	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_RAW;

	/*
	 * Make sure there are NO revision (i.e. pending object) parameter,
	 * rev.max_count is reasonable (0 <= n <= 3),
	 * there is no other revision filtering parameters.
	 */
	if (rev.pending.nr ||
	    rev.min_age != -1 || rev.max_age != -1)
		usage(diff_files_usage);
	/*
	 * Backward compatibility wart - "diff-files -s" used to
	 * defeat the common diff option "-s" which asked for
	 * DIFF_FORMAT_NO_OUTPUT.
	 */
	if (rev.diffopt.output_format == DIFF_FORMAT_NO_OUTPUT)
		rev.diffopt.output_format = DIFF_FORMAT_RAW;
	return run_diff_files(&rev, silent);
}
