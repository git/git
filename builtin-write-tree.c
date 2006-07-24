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
"git-write-tree [--missing-ok] [--prefix=<prefix>/]";

int write_tree(unsigned char *sha1, int missing_ok, const char *prefix)
{
	int entries, was_valid, newfd;

	/* We can't free this memory, it becomes part of a linked list parsed atexit() */
	struct lock_file *lock_file = xcalloc(1, sizeof(struct lock_file));

	newfd = hold_lock_file_for_update(lock_file, get_index_file());

	entries = read_cache();
	if (entries < 0)
		die("git-write-tree: error reading cache");

	if (!active_cache_tree)
		active_cache_tree = cache_tree();

	was_valid = cache_tree_fully_valid(active_cache_tree);

	if (!was_valid) {
		if (cache_tree_update(active_cache_tree,
				      active_cache, active_nr,
				      missing_ok, 0) < 0)
			die("git-write-tree: error building trees");
		if (0 <= newfd) {
			if (!write_cache(newfd, active_cache, active_nr)
					&& !close(newfd))
				commit_lock_file(lock_file);
		}
		/* Not being able to write is fine -- we are only interested
		 * in updating the cache-tree part, and if the next caller
		 * ends up using the old index with unupdated cache-tree part
		 * it misses the work we did here, but that is just a
		 * performance penalty and not a big deal.
		 */
	}

	if (prefix) {
		struct cache_tree *subtree =
			cache_tree_find(active_cache_tree, prefix);
		memcpy(sha1, subtree->sha1, 20);
	}
	else
		memcpy(sha1, active_cache_tree->sha1, 20);

	rollback_lock_file(lock_file);

	return 0;
}

int cmd_write_tree(int argc, const char **argv, char **envp)
{
	int missing_ok = 0, ret;
	const char *prefix = NULL;
	unsigned char sha1[20];

	setup_git_directory();

	while (1 < argc) {
		const char *arg = argv[1];
		if (!strcmp(arg, "--missing-ok"))
			missing_ok = 1;
		else if (!strncmp(arg, "--prefix=", 9))
			prefix = arg + 9;
		else
			die(write_tree_usage);
		argc--; argv++;
	}

	if (argc > 2)
		die("too many options");

	ret = write_tree(sha1, missing_ok, prefix);
	printf("%s\n", sha1_to_hex(sha1));

	return ret;
}
