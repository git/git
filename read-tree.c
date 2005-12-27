/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#define DBRT_DEBUG 1

#include "cache.h"

#include "object.h"
#include "tree.h"

static int merge = 0;
static int update = 0;
static int index_only = 0;
static int nontrivial_merge = 0;
static int trivial_merges_only = 0;

static int head_idx = -1;
static int merge_size = 0;

static struct object_list *trees = NULL;

static struct cache_entry df_conflict_entry = { 
};

static struct tree_entry_list df_conflict_list = {
	.name = NULL,
	.next = &df_conflict_list
};

typedef int (*merge_fn_t)(struct cache_entry **src);

static int entcmp(char *name1, int dir1, char *name2, int dir2)
{
	int len1 = strlen(name1);
	int len2 = strlen(name2);
	int len = len1 < len2 ? len1 : len2;
	int ret = memcmp(name1, name2, len);
	unsigned char c1, c2;
	if (ret)
		return ret;
	c1 = name1[len];
	c2 = name2[len];
	if (!c1 && dir1)
		c1 = '/';
	if (!c2 && dir2)
		c2 = '/';
	ret = (c1 < c2) ? -1 : (c1 > c2) ? 1 : 0;
	if (c1 && c2 && !ret)
		ret = len1 - len2;
	return ret;
}

static int unpack_trees_rec(struct tree_entry_list **posns, int len,
			    const char *base, merge_fn_t fn, int *indpos)
{
	int baselen = strlen(base);
	int src_size = len + 1;
	do {
		int i;
		char *first;
		int firstdir = 0;
		int pathlen;
		unsigned ce_size;
		struct tree_entry_list **subposns;
		struct cache_entry **src;
		int any_files = 0;
		int any_dirs = 0;
		char *cache_name;
		int ce_stage;

		/* Find the first name in the input. */

		first = NULL;
		cache_name = NULL;

		/* Check the cache */
		if (merge && *indpos < active_nr) {
			/* This is a bit tricky: */
			/* If the index has a subdirectory (with
			 * contents) as the first name, it'll get a
			 * filename like "foo/bar". But that's after
			 * "foo", so the entry in trees will get
			 * handled first, at which point we'll go into
			 * "foo", and deal with "bar" from the index,
			 * because the base will be "foo/". The only
			 * way we can actually have "foo/bar" first of
			 * all the things is if the trees don't
			 * contain "foo" at all, in which case we'll
			 * handle "foo/bar" without going into the
			 * directory, but that's fine (and will return
			 * an error anyway, with the added unknown
			 * file case.
			 */

			cache_name = active_cache[*indpos]->name;
			if (strlen(cache_name) > baselen &&
			    !memcmp(cache_name, base, baselen)) {
				cache_name += baselen;
				first = cache_name;
			} else {
				cache_name = NULL;
			}
		}

#if DBRT_DEBUG > 1
		if (first)
			printf("index %s\n", first);
#endif
		for (i = 0; i < len; i++) {
			if (!posns[i] || posns[i] == &df_conflict_list)
				continue;
#if DBRT_DEBUG > 1
			printf("%d %s\n", i + 1, posns[i]->name);
#endif
			if (!first || entcmp(first, firstdir,
					     posns[i]->name, 
					     posns[i]->directory) > 0) {
				first = posns[i]->name;
				firstdir = posns[i]->directory;
			}
		}
		/* No name means we're done */
		if (!first)
			return 0;

		pathlen = strlen(first);
		ce_size = cache_entry_size(baselen + pathlen);

		src = xmalloc(sizeof(struct cache_entry *) * src_size);
		memset(src, 0, sizeof(struct cache_entry *) * src_size);

		subposns = xmalloc(sizeof(struct tree_list_entry *) * len);
		memset(subposns, 0, sizeof(struct tree_list_entry *) * len);

		if (cache_name && !strcmp(cache_name, first)) {
			any_files = 1;
			src[0] = active_cache[*indpos];
			remove_cache_entry_at(*indpos);
		}

		for (i = 0; i < len; i++) {
			struct cache_entry *ce;

			if (!posns[i] ||
			    (posns[i] != &df_conflict_list &&
			     strcmp(first, posns[i]->name))) {
				continue;
			}

			if (posns[i] == &df_conflict_list) {
				src[i + merge] = &df_conflict_entry;
				continue;
			}

			if (posns[i]->directory) {
				any_dirs = 1;
				parse_tree(posns[i]->item.tree);
				subposns[i] = posns[i]->item.tree->entries;
				posns[i] = posns[i]->next;
				src[i + merge] = &df_conflict_entry;
				continue;
			}

			if (!merge)
				ce_stage = 0;
			else if (i + 1 < head_idx)
				ce_stage = 1;
			else if (i + 1 > head_idx)
				ce_stage = 3;
			else
				ce_stage = 2;

			ce = xmalloc(ce_size);
			memset(ce, 0, ce_size);
			ce->ce_mode = create_ce_mode(posns[i]->mode);
			ce->ce_flags = create_ce_flags(baselen + pathlen,
						       ce_stage);
			memcpy(ce->name, base, baselen);
			memcpy(ce->name + baselen, first, pathlen + 1);

			any_files = 1;

			memcpy(ce->sha1, posns[i]->item.any->sha1, 20);
			src[i + merge] = ce;
			subposns[i] = &df_conflict_list;
			posns[i] = posns[i]->next;
		}
		if (any_files) {
			if (merge) {
				int ret;

#if DBRT_DEBUG > 1
				printf("%s:\n", first);
				for (i = 0; i < src_size; i++) {
					printf(" %d ", i);
					if (src[i])
						printf("%s\n", sha1_to_hex(src[i]->sha1));
					else
						printf("\n");
				}
#endif
				ret = fn(src);
				
#if DBRT_DEBUG > 1
				printf("Added %d entries\n", ret);
#endif
				*indpos += ret;
			} else {
				for (i = 0; i < src_size; i++) {
					if (src[i]) {
						add_cache_entry(src[i], ADD_CACHE_OK_TO_ADD|ADD_CACHE_SKIP_DFCHECK);
					}
				}
			}
		}
		if (any_dirs) {
			char *newbase = xmalloc(baselen + 2 + pathlen);
			memcpy(newbase, base, baselen);
			memcpy(newbase + baselen, first, pathlen);
			newbase[baselen + pathlen] = '/';
			newbase[baselen + pathlen + 1] = '\0';
			if (unpack_trees_rec(subposns, len, newbase, fn,
					     indpos))
				return -1;
			free(newbase);
		}
		free(subposns);
		free(src);
	} while (1);
}

static void reject_merge(struct cache_entry *ce)
{
	die("Entry '%s' would be overwritten by merge. Cannot merge.", 
	    ce->name);
}

/* Unlink the last component and attempt to remove leading
 * directories, in case this unlink is the removal of the
 * last entry in the directory -- empty directories are removed.
 */
static void unlink_entry(char *name)
{
	char *cp, *prev;

	if (unlink(name))
		return;
	prev = NULL;
	while (1) {
		int status;
		cp = strrchr(name, '/');
		if (prev)
			*prev = '/';
		if (!cp)
			break;

		*cp = 0;
		status = rmdir(name);
		if (status) {
			*cp = '/';
			break;
		}
		prev = cp;
	}
}

static void check_updates(struct cache_entry **src, int nr)
{
	static struct checkout state = {
		.base_dir = "",
		.force = 1,
		.quiet = 1,
		.refresh_cache = 1,
	};
	unsigned short mask = htons(CE_UPDATE);
	while (nr--) {
		struct cache_entry *ce = *src++;
		if (!ce->ce_mode) {
			if (update)
				unlink_entry(ce->name);
			continue;
		}
		if (ce->ce_flags & mask) {
			ce->ce_flags &= ~mask;
			if (update)
				checkout_entry(ce, &state);
		}
	}
}

static int unpack_trees(merge_fn_t fn)
{
	int indpos = 0;
	unsigned len = object_list_length(trees);
	struct tree_entry_list **posns;
	int i;
	struct object_list *posn = trees;
	merge_size = len;

	if (len) {
		posns = xmalloc(len * sizeof(struct tree_entry_list *));
		for (i = 0; i < len; i++) {
			posns[i] = ((struct tree *) posn->item)->entries;
			posn = posn->next;
		}
		if (unpack_trees_rec(posns, len, "", fn, &indpos))
			return -1;
	}

	if (trivial_merges_only && nontrivial_merge)
		die("Merge requires file-level merging");

	check_updates(active_cache, active_nr);
	return 0;
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
static void verify_uptodate(struct cache_entry *ce)
{
	struct stat st;

	if (index_only)
		return;

	if (!lstat(ce->name, &st)) {
		unsigned changed = ce_match_stat(ce, &st);
		if (!changed)
			return;
		errno = 0;
	}
	if (errno == ENOENT)
		return;
	die("Entry '%s' not uptodate. Cannot merge.", ce->name);
}

static int merged_entry(struct cache_entry *merge, struct cache_entry *old)
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
			verify_uptodate(old);
		}
	}
	merge->ce_flags &= ~htons(CE_STAGEMASK);
	add_cache_entry(merge, ADD_CACHE_OK_TO_ADD);
	return 1;
}

static int deleted_entry(struct cache_entry *ce, struct cache_entry *old)
{
	if (old)
		verify_uptodate(old);
	ce->ce_mode = 0;
	add_cache_entry(ce, ADD_CACHE_OK_TO_ADD);
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

static int threeway_merge(struct cache_entry **stages)
{
	struct cache_entry *index;
	struct cache_entry *head; 
	struct cache_entry *remote = stages[head_idx + 1];
	int count;
	int head_match = 0;
	int remote_match = 0;

	int df_conflict_head = 0;
	int df_conflict_remote = 0;

	int any_anc_missing = 0;
	int i;

	for (i = 1; i < head_idx; i++) {
		if (!stages[i])
			any_anc_missing = 1;
	}

	index = stages[0];
	head = stages[head_idx];

	if (head == &df_conflict_entry) {
		df_conflict_head = 1;
		head = NULL;
	}

	if (remote == &df_conflict_entry) {
		df_conflict_remote = 1;
		remote = NULL;
	}

	/* First, if there's a #16 situation, note that to prevent #13
	 * and #14. 
	 */
	if (!same(remote, head)) {
		for (i = 1; i < head_idx; i++) {
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
		return merged_entry(remote, index);
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
			return merged_entry(head, index);
		/* #13, #3ALT */
		if (!df_conflict_remote && remote_match && !head_match)
			return merged_entry(head, index);
	}

	/* #1 */
	if (!head && !remote && any_anc_missing)
		return 0;

	/* Below are "no merge" cases, which require that the index be
	 * up-to-date to avoid the files getting overwritten with
	 * conflict resolution files. 
	 */
	if (index) {
		verify_uptodate(index);
	}

	nontrivial_merge = 1;

	/* #2, #3, #4, #6, #7, #9, #11. */
	count = 0;
	if (!head_match || !remote_match) {
		for (i = 1; i < head_idx; i++) {
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
static int twoway_merge(struct cache_entry **src)
{
	struct cache_entry *current = src[0];
	struct cache_entry *oldtree = src[1], *newtree = src[2];

	if (merge_size != 2)
		return error("Cannot do a twoway merge of %d trees\n",
			     merge_size);

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
			return deleted_entry(oldtree, current);
		}
		else if (oldtree && newtree &&
			 same(current, oldtree) && !same(current, newtree)) {
			/* 20 or 21 */
			return merged_entry(newtree, current);
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
		return merged_entry(newtree, current);
	else
		return deleted_entry(oldtree, current);
}

/*
 * One-way merge.
 *
 * The rule is:
 * - take the stat information from stage0, take the data from stage1
 */
static int oneway_merge(struct cache_entry **src)
{
	struct cache_entry *old = src[0];
	struct cache_entry *a = src[1];

	if (merge_size != 1)
		return error("Cannot do a oneway merge of %d trees\n",
			     merge_size);

	if (!a)
		return 0;
	if (old && same(old, a)) {
		return keep_entry(old);
	}
	return merged_entry(a, NULL);
}

static int read_cache_unmerged(void)
{
	int i, deleted;
	struct cache_entry **dst;

	read_cache();
	dst = active_cache;
	deleted = 0;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (ce_stage(ce)) {
			deleted++;
			continue;
		}
		if (deleted)
			*dst = ce;
		dst++;
	}
	active_nr -= deleted;
	return deleted;
}

static const char read_tree_usage[] = "git-read-tree (<sha> | -m [-u | -i] <sha1> [<sha2> [<sha3>]])";

static struct cache_file cache_file;

int main(int argc, char **argv)
{
	int i, newfd, reset, stage = 0;
	unsigned char sha1[20];
	merge_fn_t fn = NULL;

	setup_git_directory();

	newfd = hold_index_file_for_update(&cache_file, get_index_file());
	if (newfd < 0)
		die("unable to create new cachefile");

	git_config(git_default_config);

	merge = 0;
	reset = 0;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		/* "-u" means "update", meaning that a merge will update
		 * the working tree.
		 */
		if (!strcmp(arg, "-u")) {
			update = 1;
			continue;
		}

		/* "-i" means "index only", meaning that a merge will
		 * not even look at the working tree.
		 */
		if (!strcmp(arg, "-i")) {
			index_only = 1;
			continue;
		}

		/* This differs from "-m" in that we'll silently ignore unmerged entries */
		if (!strcmp(arg, "--reset")) {
			if (stage || merge)
				usage(read_tree_usage);
			reset = 1;
			merge = 1;
			stage = 1;
			read_cache_unmerged();
			continue;
		}

		if (!strcmp(arg, "--trivial")) {
			trivial_merges_only = 1;
			continue;
		}

		/* "-m" stands for "merge", meaning we start in stage 1 */
		if (!strcmp(arg, "-m")) {
			if (stage || merge)
				usage(read_tree_usage);
			if (read_cache_unmerged())
				die("you need to resolve your current index first");
			stage = 1;
			merge = 1;
			continue;
		}

		/* using -u and -i at the same time makes no sense */
		if (1 < index_only + update)
			usage(read_tree_usage);

		if (get_sha1(arg, sha1) < 0)
			usage(read_tree_usage);
		if (list_tree(sha1) < 0)
			die("failed to unpack tree object %s", arg);
		stage++;
	}
	if ((update||index_only) && !merge)
		usage(read_tree_usage);

	if (merge) {
		if (stage < 2)
			die("just how do you expect me to merge %d trees?", stage-1);
		switch (stage - 1) {
		case 1:
			fn = oneway_merge;
			break;
		case 2:
			fn = twoway_merge;
			break;
		case 3:
			fn = threeway_merge;
			break;
		default:
			fn = threeway_merge;
			break;
		}

		if (stage - 1 >= 3)
			head_idx = stage - 2;
		else
			head_idx = 1;
	}

	unpack_trees(fn);
	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_index_file(&cache_file))
		die("unable to write new index file");
	return 0;
}
