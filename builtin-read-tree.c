/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */

#include "cache.h"
#include "object.h"
#include "tree.h"
#include "tree-walk.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "dir.h"
#include "builtin.h"

static int nr_trees;
static struct tree *trees[MAX_UNPACK_TREES];

static int list_tree(unsigned char *sha1)
{
	struct tree *tree;

	if (nr_trees >= MAX_UNPACK_TREES)
		die("I cannot read more than %d trees", MAX_UNPACK_TREES);
	tree = parse_tree_indirect(sha1);
	if (!tree)
		return -1;
	trees[nr_trees++] = tree;
	return 0;
}

static const char read_tree_usage[] = "git read-tree (<sha> | [[-m [--trivial] [--aggressive] | --reset | --prefix=<prefix>] [-u | -i]] [--exclude-per-directory=<gitignore>] [--index-output=<file>] <sha1> [<sha2> [<sha3>]])";

static struct lock_file lock_file;

int cmd_read_tree(int argc, const char **argv, const char *unused_prefix)
{
	int i, newfd, stage = 0;
	unsigned char sha1[20];
	struct tree_desc t[MAX_UNPACK_TREES];
	struct unpack_trees_options opts;

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = -1;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;

	git_config(git_default_config, NULL);

	newfd = hold_locked_index(&lock_file, 1);

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		/* "-u" means "update", meaning that a merge will update
		 * the working tree.
		 */
		if (!strcmp(arg, "-u")) {
			opts.update = 1;
			continue;
		}

		if (!strcmp(arg, "-v")) {
			opts.verbose_update = 1;
			continue;
		}

		/* "-i" means "index only", meaning that a merge will
		 * not even look at the working tree.
		 */
		if (!strcmp(arg, "-i")) {
			opts.index_only = 1;
			continue;
		}

		if (!prefixcmp(arg, "--index-output=")) {
			set_alternate_index_output(arg + 15);
			continue;
		}

		/* "--prefix=<subdirectory>/" means keep the current index
		 *  entries and put the entries from the tree under the
		 * given subdirectory.
		 */
		if (!prefixcmp(arg, "--prefix=")) {
			if (stage || opts.merge || opts.prefix)
				usage(read_tree_usage);
			opts.prefix = arg + 9;
			opts.merge = 1;
			stage = 1;
			if (read_cache_unmerged())
				die("you need to resolve your current index first");
			continue;
		}

		/* This differs from "-m" in that we'll silently ignore
		 * unmerged entries and overwrite working tree files that
		 * correspond to them.
		 */
		if (!strcmp(arg, "--reset")) {
			if (stage || opts.merge || opts.prefix)
				usage(read_tree_usage);
			opts.reset = 1;
			opts.merge = 1;
			stage = 1;
			read_cache_unmerged();
			continue;
		}

		if (!strcmp(arg, "--trivial")) {
			opts.trivial_merges_only = 1;
			continue;
		}

		if (!strcmp(arg, "--aggressive")) {
			opts.aggressive = 1;
			continue;
		}

		/* "-m" stands for "merge", meaning we start in stage 1 */
		if (!strcmp(arg, "-m")) {
			if (stage || opts.merge || opts.prefix)
				usage(read_tree_usage);
			if (read_cache_unmerged())
				die("you need to resolve your current index first");
			stage = 1;
			opts.merge = 1;
			continue;
		}

		if (!prefixcmp(arg, "--exclude-per-directory=")) {
			struct dir_struct *dir;

			if (opts.dir)
				die("more than one --exclude-per-directory are given.");

			dir = xcalloc(1, sizeof(*opts.dir));
			dir->flags |= DIR_SHOW_IGNORED;
			dir->exclude_per_dir = arg + 24;
			opts.dir = dir;
			/* We do not need to nor want to do read-directory
			 * here; we are merely interested in reusing the
			 * per directory ignore stack mechanism.
			 */
			continue;
		}

		/* using -u and -i at the same time makes no sense */
		if (1 < opts.index_only + opts.update)
			usage(read_tree_usage);

		if (get_sha1(arg, sha1))
			die("Not a valid object name %s", arg);
		if (list_tree(sha1) < 0)
			die("failed to unpack tree object %s", arg);
		stage++;
	}
	if ((opts.update||opts.index_only) && !opts.merge)
		usage(read_tree_usage);
	if ((opts.dir && !opts.update))
		die("--exclude-per-directory is meaningless unless -u");
	if (opts.merge && !opts.index_only)
		setup_work_tree();

	if (opts.merge) {
		if (stage < 2)
			die("just how do you expect me to merge %d trees?", stage-1);
		switch (stage - 1) {
		case 1:
			opts.fn = opts.prefix ? bind_merge : oneway_merge;
			break;
		case 2:
			opts.fn = twoway_merge;
			opts.initial_checkout = is_cache_unborn();
			break;
		case 3:
		default:
			opts.fn = threeway_merge;
			break;
		}

		if (stage - 1 >= 3)
			opts.head_idx = stage - 2;
		else
			opts.head_idx = 1;
	}

	cache_tree_free(&active_cache_tree);
	for (i = 0; i < nr_trees; i++) {
		struct tree *tree = trees[i];
		parse_tree(tree);
		init_tree_desc(t+i, tree->buffer, tree->size);
	}
	if (unpack_trees(nr_trees, t, &opts))
		return 128;

	/*
	 * When reading only one tree (either the most basic form,
	 * "-m ent" or "--reset ent" form), we can obtain a fully
	 * valid cache-tree because the index must match exactly
	 * what came from the tree.
	 *
	 * The same holds true if we are switching between two trees
	 * using read-tree -m A B.  The index must match B after that.
	 */
	if (nr_trees == 1 && !opts.prefix)
		prime_cache_tree(&active_cache_tree, trees[0]);
	else if (nr_trees == 2 && opts.merge)
		prime_cache_tree(&active_cache_tree, trees[1]);

	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_locked_index(&lock_file))
		die("unable to write new index file");
	return 0;
}
