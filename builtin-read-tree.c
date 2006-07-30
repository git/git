/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#define DBRT_DEBUG 1

#include "cache.h"

#include "object.h"
#include "tree.h"
#include "tree-walk.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "builtin.h"

static struct object_list *trees = NULL;

static void reject_merge(struct cache_entry *ce)
{
	die("Entry '%s' would be overwritten by merge. Cannot merge.",
	    ce->name);
}

static int list_tree(unsigned char *sha1)
{
	struct tree *tree = parse_tree_indirect(sha1);
	if (!tree)
		return -1;
	object_list_append(&tree->object, &trees);
	return 0;
}

static int same(struct cache_entry *a, struct cache_entry *b)
{
	if (!!a != !!b)
		return 0;
	if (!a && !b)
		return 1;
	return a->ce_mode == b->ce_mode && 
		!memcmp(a->sha1, b->sha1, 20);
}


/*
 * When a CE gets turned into an unmerged entry, we
 * want it to be up-to-date
 */
static void verify_uptodate(struct cache_entry *ce,
		struct unpack_trees_options *o)
{
	struct stat st;

	if (o->index_only || o->reset)
		return;

	if (!lstat(ce->name, &st)) {
		unsigned changed = ce_match_stat(ce, &st, 1);
		if (!changed)
			return;
		errno = 0;
	}
	if (o->reset) {
		ce->ce_flags |= htons(CE_UPDATE);
		return;
	}
	if (errno == ENOENT)
		return;
	die("Entry '%s' not uptodate. Cannot merge.", ce->name);
}

static void invalidate_ce_path(struct cache_entry *ce)
{
	if (ce)
		cache_tree_invalidate_path(active_cache_tree, ce->name);
}

/*
 * We do not want to remove or overwrite a working tree file that
 * is not tracked.
 */
static void verify_absent(const char *path, const char *action,
		struct unpack_trees_options *o)
{
	struct stat st;

	if (o->index_only || o->reset || !o->update)
		return;
	if (!lstat(path, &st))
		die("Untracked working tree file '%s' "
		    "would be %s by merge.", path, action);
}

static int merged_entry(struct cache_entry *merge, struct cache_entry *old,
		struct unpack_trees_options *o)
{
	merge->ce_flags |= htons(CE_UPDATE);
	if (old) {
		/*
		 * See if we can re-use the old CE directly?
		 * That way we get the uptodate stat info.
		 *
		 * This also removes the UPDATE flag on
		 * a match.
		 */
		if (same(old, merge)) {
			*merge = *old;
		} else {
			verify_uptodate(old, o);
			invalidate_ce_path(old);
		}
	}
	else {
		verify_absent(merge->name, "overwritten", o);
		invalidate_ce_path(merge);
	}

	merge->ce_flags &= ~htons(CE_STAGEMASK);
	add_cache_entry(merge, ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE);
	return 1;
}

static int deleted_entry(struct cache_entry *ce, struct cache_entry *old,
		struct unpack_trees_options *o)
{
	if (old)
		verify_uptodate(old, o);
	else
		verify_absent(ce->name, "removed", o);
	ce->ce_mode = 0;
	add_cache_entry(ce, ADD_CACHE_OK_TO_ADD|ADD_CACHE_OK_TO_REPLACE);
	invalidate_ce_path(ce);
	return 1;
}

static int keep_entry(struct cache_entry *ce)
{
	add_cache_entry(ce, ADD_CACHE_OK_TO_ADD);
	return 1;
}

#if DBRT_DEBUG
static void show_stage_entry(FILE *o,
			     const char *label, const struct cache_entry *ce)
{
	if (!ce)
		fprintf(o, "%s (missing)\n", label);
	else
		fprintf(o, "%s%06o %s %d\t%s\n",
			label,
			ntohl(ce->ce_mode),
			sha1_to_hex(ce->sha1),
			ce_stage(ce),
			ce->name);
}
#endif

static int threeway_merge(struct cache_entry **stages,
		struct unpack_trees_options *o)
{
	struct cache_entry *index;
	struct cache_entry *head;
	struct cache_entry *remote = stages[o->head_idx + 1];
	int count;
	int head_match = 0;
	int remote_match = 0;
	const char *path = NULL;

	int df_conflict_head = 0;
	int df_conflict_remote = 0;

	int any_anc_missing = 0;
	int no_anc_exists = 1;
	int i;

	for (i = 1; i < o->head_idx; i++) {
		if (!stages[i])
			any_anc_missing = 1;
		else {
			if (!path)
				path = stages[i]->name;
			no_anc_exists = 0;
		}
	}

	index = stages[0];
	head = stages[o->head_idx];

	if (head == &o->df_conflict_entry) {
		df_conflict_head = 1;
		head = NULL;
	}

	if (remote == &o->df_conflict_entry) {
		df_conflict_remote = 1;
		remote = NULL;
	}

	if (!path && index)
		path = index->name;
	if (!path && head)
		path = head->name;
	if (!path && remote)
		path = remote->name;

	/* First, if there's a #16 situation, note that to prevent #13
	 * and #14.
	 */
	if (!same(remote, head)) {
		for (i = 1; i < o->head_idx; i++) {
			if (same(stages[i], head)) {
				head_match = i;
			}
			if (same(stages[i], remote)) {
				remote_match = i;
			}
		}
	}

	/* We start with cases where the index is allowed to match
	 * something other than the head: #14(ALT) and #2ALT, where it
	 * is permitted to match the result instead.
	 */
	/* #14, #14ALT, #2ALT */
	if (remote && !df_conflict_head && head_match && !remote_match) {
		if (index && !same(index, remote) && !same(index, head))
			reject_merge(index);
		return merged_entry(remote, index, o);
	}
	/*
	 * If we have an entry in the index cache, then we want to
	 * make sure that it matches head.
	 */
	if (index && !same(index, head)) {
		reject_merge(index);
	}

	if (head) {
		/* #5ALT, #15 */
		if (same(head, remote))
			return merged_entry(head, index, o);
		/* #13, #3ALT */
		if (!df_conflict_remote && remote_match && !head_match)
			return merged_entry(head, index, o);
	}

	/* #1 */
	if (!head && !remote && any_anc_missing)
		return 0;

	/* Under the new "aggressive" rule, we resolve mostly trivial
	 * cases that we historically had git-merge-one-file resolve.
	 */
	if (o->aggressive) {
		int head_deleted = !head && !df_conflict_head;
		int remote_deleted = !remote && !df_conflict_remote;
		/*
		 * Deleted in both.
		 * Deleted in one and unchanged in the other.
		 */
		if ((head_deleted && remote_deleted) ||
		    (head_deleted && remote && remote_match) ||
		    (remote_deleted && head && head_match)) {
			if (index)
				return deleted_entry(index, index, o);
			else if (path)
				verify_absent(path, "removed", o);
			return 0;
		}
		/*
		 * Added in both, identically.
		 */
		if (no_anc_exists && head && remote && same(head, remote))
			return merged_entry(head, index, o);

	}

	/* Below are "no merge" cases, which require that the index be
	 * up-to-date to avoid the files getting overwritten with
	 * conflict resolution files.
	 */
	if (index) {
		verify_uptodate(index, o);
	}
	else if (path)
		verify_absent(path, "overwritten", o);

	o->nontrivial_merge = 1;

	/* #2, #3, #4, #6, #7, #9, #11. */
	count = 0;
	if (!head_match || !remote_match) {
		for (i = 1; i < o->head_idx; i++) {
			if (stages[i]) {
				keep_entry(stages[i]);
				count++;
				break;
			}
		}
	}
#if DBRT_DEBUG
	else {
		fprintf(stderr, "read-tree: warning #16 detected\n");
		show_stage_entry(stderr, "head   ", stages[head_match]);
		show_stage_entry(stderr, "remote ", stages[remote_match]);
	}
#endif
	if (head) { count += keep_entry(head); }
	if (remote) { count += keep_entry(remote); }
	return count;
}

/*
 * Two-way merge.
 *
 * The rule is to "carry forward" what is in the index without losing
 * information across a "fast forward", favoring a successful merge
 * over a merge failure when it makes sense.  For details of the
 * "carry forward" rule, please see <Documentation/git-read-tree.txt>.
 *
 */
static int twoway_merge(struct cache_entry **src,
		struct unpack_trees_options *o)
{
	struct cache_entry *current = src[0];
	struct cache_entry *oldtree = src[1], *newtree = src[2];

	if (o->merge_size != 2)
		return error("Cannot do a twoway merge of %d trees",
			     o->merge_size);

	if (current) {
		if ((!oldtree && !newtree) || /* 4 and 5 */
		    (!oldtree && newtree &&
		     same(current, newtree)) || /* 6 and 7 */
		    (oldtree && newtree &&
		     same(oldtree, newtree)) || /* 14 and 15 */
		    (oldtree && newtree &&
		     !same(oldtree, newtree) && /* 18 and 19*/
		     same(current, newtree))) {
			return keep_entry(current);
		}
		else if (oldtree && !newtree && same(current, oldtree)) {
			/* 10 or 11 */
			return deleted_entry(oldtree, current, o);
		}
		else if (oldtree && newtree &&
			 same(current, oldtree) && !same(current, newtree)) {
			/* 20 or 21 */
			return merged_entry(newtree, current, o);
		}
		else {
			/* all other failures */
			if (oldtree)
				reject_merge(oldtree);
			if (current)
				reject_merge(current);
			if (newtree)
				reject_merge(newtree);
			return -1;
		}
	}
	else if (newtree)
		return merged_entry(newtree, current, o);
	else
		return deleted_entry(oldtree, current, o);
}

/*
 * Bind merge.
 *
 * Keep the index entries at stage0, collapse stage1 but make sure
 * stage0 does not have anything there.
 */
static int bind_merge(struct cache_entry **src,
		struct unpack_trees_options *o)
{
	struct cache_entry *old = src[0];
	struct cache_entry *a = src[1];

	if (o->merge_size != 1)
		return error("Cannot do a bind merge of %d trees\n",
			     o->merge_size);
	if (a && old)
		die("Entry '%s' overlaps.  Cannot bind.", a->name);
	if (!a)
		return keep_entry(old);
	else
		return merged_entry(a, NULL, o);
}

/*
 * One-way merge.
 *
 * The rule is:
 * - take the stat information from stage0, take the data from stage1
 */
static int oneway_merge(struct cache_entry **src,
		struct unpack_trees_options *o)
{
	struct cache_entry *old = src[0];
	struct cache_entry *a = src[1];

	if (o->merge_size != 1)
		return error("Cannot do a oneway merge of %d trees",
			     o->merge_size);

	if (!a)
		return deleted_entry(old, old, o);
	if (old && same(old, a)) {
		if (o->reset) {
			struct stat st;
			if (lstat(old->name, &st) ||
			    ce_match_stat(old, &st, 1))
				old->ce_flags |= htons(CE_UPDATE);
		}
		return keep_entry(old);
	}
	return merged_entry(a, old, o);
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
			invalidate_ce_path(ce);
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

	memcpy(it->sha1, tree->object.sha1, 20);
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

int cmd_read_tree(int argc, const char **argv, const char *prefix)
{
	int i, newfd, stage = 0;
	unsigned char sha1[20];
	struct unpack_trees_options opts;

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = -1;

	setup_git_directory();
	git_config(git_default_config);

	newfd = hold_lock_file_for_update(&lock_file, get_index_file());
	if (newfd < 0)
		die("unable to create new index file");

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
