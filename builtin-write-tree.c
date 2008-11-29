/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "builtin.h"
#include "cache.h"
#include "tree.h"
#include "cache-tree.h"

static const char write_tree_usage[] =
"git write-tree [--missing-ok] [--prefix=<prefix>/]";

int cmd_write_tree(int argc, const char **argv, const char *unused_prefix)
{
	int missing_ok = 0, ret;
	const char *prefix = NULL;
	unsigned char sha1[20];
	const char *me = "git-write-tree";

	git_config(git_default_config, NULL);
	while (1 < argc) {
		const char *arg = argv[1];
		if (!strcmp(arg, "--missing-ok"))
			missing_ok = 1;
		else if (!prefixcmp(arg, "--prefix="))
			prefix = arg + 9;
		else
			usage(write_tree_usage);
		argc--; argv++;
	}

	if (argc > 2)
		die("too many options");

	ret = write_cache_as_tree(sha1, missing_ok, prefix);
	switch (ret) {
	case 0:
		printf("%s\n", sha1_to_hex(sha1));
		break;
	case WRITE_TREE_UNREADABLE_INDEX:
		die("%s: error reading the index", me);
		break;
	case WRITE_TREE_UNMERGED_INDEX:
		die("%s: error building trees", me);
		break;
	case WRITE_TREE_PREFIX_ERROR:
		die("%s: prefix %s not found", me, prefix);
		break;
	}
	return ret;
}
