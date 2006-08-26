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
#include "builtin.h"

static struct object_list *trees;

static int list_tree(unsigned char *sha1)
{
	struct tree *tree = parse_tree_indirect(sha1);
	if (!tree)
		return -1;
	object_list_append(&tree->object, &trees);
	return 0;
}

static int read_cache_unmerged(void)
{
	int i;
	struct cache_entry **dst;
	struct cache_entry *last = NULL;

	read_cache();
	dst = active_cache;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ce_stage(ce)) {
			if (last && !strcmp(ce->name, last->name))
				continue;
			cache_tree_invalidate_path(active_cache_tree, ce->name);
			last = ce;
			ce->ce_mode = 0;
			ce->ce_flags &= ~htons(CE_STAGEMASK);
		}
		*dst++ = ce;
	}
	active_nr = dst - active_cache;
	return !!last;
}

static void prime_cache_tree_rec(struct cache_tree *it, struct tree *tree)
{
	struct tree_desc desc;
	struct name_entry entry;
	int cnt;

	hashcpy(it->sha1, tree->object.sha1);
	desc.buf = tree->buffer;
	desc.size = tree->size;
	cnt = 0;
	while (tree_entry(&desc, &entry)) {
		if (!S_ISDIR(entry.mode))
			cnt++;
		else {
			struct cache_tree_sub *sub;
			struct tree *subtree = lookup_tree(entry.sha1);
			if (!subtree->object.parsed)
				parse_tree(subtree);
			sub = cache_tree_sub(it, entry.path);
			sub->cache_tree = cache_tree();
			prime_cache_tree_rec(sub->cache_tree, subtree);
			cnt += sub->cache_tree->entry_count;
		}
	}
	it->entry_count = cnt;
}

static void prime_cache_tree(void)
{
	struct tree *tree = (struct tree *)trees->item;
	if (!tree)
		return;
	active_cache_tree = cache_tree();
	prime_cache_tree_rec(active_cache_tree, tree);

}

static const char read_tree_usage[] = "git-read-tree (<sha> | [[-m [--aggressive] | --reset | --prefix=<prefix>] [-u | -i]] <sha1> [<sha2> [<sha3>]])";

static struct lock_file lock_file;

int cmd_read_tree(int argc, const char **argv, const char *unused_prefix)
{
	int i, newfd, stage = 0;
	unsigned char sha1[20];
	struct unpack_trees_options opts;

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = -1;

	setup_git_directory();
	git_config(git_default_config);

	newfd = hold_lock_file_for_update(&lock_file, get_index_file(), 1);

	git_config(git_default_config);

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

		/* "--prefix=<subdirectory>/" means keep the current index
		 *  entries and put the entries from the tree under the
		 * given subdirectory.
		 */
		if (!strncmp(arg, "--prefix=", 9)) {
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

	if (opts.prefix) {
		int pfxlen = strlen(opts.prefix);
		int pos;
		if (opts.prefix[pfxlen-1] != '/')
			die("prefix must end with /");
		if (stage != 2)
			die("binding merge takes only one tree");
		pos = cache_name_pos(opts.prefix, pfxlen);
		if (0 <= pos)
			die("corrupt index file");
		pos = -pos-1;
		if (pos < active_nr &&
		    !strncmp(active_cache[pos]->name, opts.prefix, pfxlen))
			die("subdirectory '%s' already exists.", opts.prefix);
		pos = cache_name_pos(opts.prefix, pfxlen-1);
		if (0 <= pos)
			die("file '%.*s' already exists.",
					pfxlen-1, opts.prefix);
	}

	if (opts.merge) {
		if (stage < 2)
			die("just how do you expect me to merge %d trees?", stage-1);
		switch (stage - 1) {
		case 1:
			opts.fn = opts.prefix ? bind_merge : oneway_merge;
			break;
		case 2:
			opts.fn = twoway_merge;
			break;
		case 3:
		default:
			opts.fn = threeway_merge;
			cache_tree_free(&active_cache_tree);
			break;
		}

		if (stage - 1 >= 3)
			opts.head_idx = stage - 2;
		else
			opts.head_idx = 1;
	}

	unpack_trees(trees, &opts);

	/*
	 * When reading only one tree (either the most basic form,
	 * "-m ent" or "--reset ent" form), we can obtain a fully
	 * valid cache-tree because the index must match exactly
	 * what came from the tree.
	 */
	if (trees && trees->item && !opts.prefix && (!opts.merge || (stage == 2))) {
		cache_tree_free(&active_cache_tree);
		prime_cache_tree();
	}

	if (write_cache(newfd, active_cache, active_nr) ||
	    close(newfd) || commit_lock_file(&lock_file))
		die("unable to write new index file");
	return 0;
}
