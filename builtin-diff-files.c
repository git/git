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
"git-diff-files [-q] [-0/-1/2/3 |-c|--cc|-n|--no-index] [<common diff options>] [<path>...]"
COMMON_DIFF_OPTIONS_HELP;

int cmd_diff_files(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	int nongit = 0;

	prefix = setup_git_directory_gently(&nongit);
	init_revisions(&rev, prefix);
	git_config(git_default_config); /* no "diff" UI options */
	rev.abbrev = 0;

	if (!setup_diff_no_index(&rev, argc, argv, nongit, prefix))
		argc = 0;
	else
		argc = setup_revisions(argc, argv, &rev, NULL);
	if (!rev.diffopt.output_format)
		rev.diffopt.output_format = DIFF_FORMAT_RAW;
	return run_diff_files_cmd(&rev, argc, argv);
}
