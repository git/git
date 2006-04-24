/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "tree.h"
#include "cache-tree.h"

static unsigned char active_cache_sha1[20];
static struct cache_tree *active_cache_tree;

static int missing_ok = 0;

static const char write_tree_usage[] = "git-write-tree [--missing-ok]";

int main(int argc, char **argv)
{
	int entries;

	setup_git_directory();

	entries = read_cache_1(active_cache_sha1);
	active_cache_tree = read_cache_tree(active_cache_sha1);
	if (argc == 2) {
		if (!strcmp(argv[1], "--missing-ok"))
			missing_ok = 1;
		else
			die(write_tree_usage);
	}
	
	if (argc > 2)
		die("too many options");

	if (entries < 0)
		die("git-write-tree: error reading cache");

	if (cache_tree_update(active_cache_tree, active_cache, active_nr,
			      missing_ok))
		die("git-write-tree: error building trees");
	write_cache_tree(active_cache_sha1, active_cache_tree);

	printf("%s\n", sha1_to_hex(active_cache_tree->sha1));
	return 0;
}
