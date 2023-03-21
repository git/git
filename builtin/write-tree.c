/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#define USE_THE_INDEX_VARIABLE
#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "tree.h"
#include "cache-tree.h"
#include "parse-options.h"

static const char * const write_tree_usage[] = {
	N_("git write-tree [--missing-ok] [--prefix=<prefix>/]"),
	NULL
};

int cmd_write_tree(int argc, const char **argv, const char *cmd_prefix)
{
	int flags = 0, ret;
	const char *tree_prefix = NULL;
	struct object_id oid;
	const char *me = "git-write-tree";
	struct option write_tree_options[] = {
		OPT_BIT(0, "missing-ok", &flags, N_("allow missing objects"),
			WRITE_TREE_MISSING_OK),
		OPT_STRING(0, "prefix", &tree_prefix, N_("<prefix>/"),
			   N_("write tree object for a subdirectory <prefix>")),
		{ OPTION_BIT, 0, "ignore-cache-tree", &flags, NULL,
		  N_("only useful for debugging"),
		  PARSE_OPT_HIDDEN | PARSE_OPT_NOARG, NULL,
		  WRITE_TREE_IGNORE_CACHE_TREE },
		OPT_END()
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, cmd_prefix, write_tree_options,
			     write_tree_usage, 0);

	ret = write_index_as_tree(&oid, &the_index, get_index_file(), flags,
				  tree_prefix);
	switch (ret) {
	case 0:
		printf("%s\n", oid_to_hex(&oid));
		break;
	case WRITE_TREE_UNREADABLE_INDEX:
		die("%s: error reading the index", me);
		break;
	case WRITE_TREE_UNMERGED_INDEX:
		die("%s: error building trees", me);
		break;
	case WRITE_TREE_PREFIX_ERROR:
		die("%s: prefix %s not found", me, tree_prefix);
		break;
	}
	return ret;
}
