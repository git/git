/*
 * Implementation of git-merge-ours.sh as builtin
 *
 * Copyright (c) 2007 Thomas Harning Jr
 * Original:
 * Original Copyright (c) 2005 Junio C Hamano
 *
 * Pretend we resolved the heads, but declare our tree trumps everybody else.
 */
#include "git-compat-util.h"
#include "builtin.h"

static const char builtin_merge_ours_usage[] =
	"git merge-ours <base>... -- HEAD <remote>...";

static const char *diff_index_args[] = {
	"diff-index", "--quiet", "--cached", "HEAD", "--", NULL
};
#define NARGS (ARRAY_SIZE(diff_index_args) - 1)

int cmd_merge_ours(int argc, const char **argv, const char *prefix)
{
	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(builtin_merge_ours_usage);

	/*
	 * We need to exit with 2 if the index does not match our HEAD tree,
	 * because the current index is what we will be committing as the
	 * merge result.
	 */
	if (cmd_diff_index(NARGS, diff_index_args, prefix))
		exit(2);
	exit(0);
}
