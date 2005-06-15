/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static int stage = 0;
static int update = 0;

static int unpack_tree(unsigned char *sha1)
{
	void *buffer;
	unsigned long size;
	int ret;

	buffer = read_object_with_reference(sha1, "tree", &size, NULL);
	if (!buffer)
		return -1;
	ret = read_tree(buffer, size, stage);
	free(buffer);
	return ret;
}

static int path_matches(struct cache_entry *a, struct cache_entry *b)
{
	int len = ce_namelen(a);
	return ce_namelen(b) == len &&
		!memcmp(a->name, b->name, len);
}

static int same(struct cache_entry *a, struct cache_entry *b)
{
	return a->ce_mode == b->ce_mode && 
		!memcmp(a->sha1, b->sha1, 20);
}


/*
 * This removes all trivial merges that don't change the tree
 * and collapses them to state 0.
 */
static struct cache_entry *merge_entries(struct cache_entry *a,
					 struct cache_entry *b,
					 struct cache_entry *c)
{
	/*
	 * Ok, all three entries describe the same
	 * filename, but maybe the contents or file
	 * mode have changed?
	 *
	 * The trivial cases end up being the ones where two
	 * out of three files are the same:
	 *  - both destinations the same, trivially take either
	 *  - one of the destination versions hasn't changed,
	 *    take the other.
	 *
	 * The "all entries exactly the same" case falls out as
	 * a special case of any of the "two same" cases.
	 *
	 * Here "a" is "original", and "b" and "c" are the two
	 * trees we are merging.
	 */
	if (a && b && c) {
		if (same(b,c))
			return c;
		if (same(a,b))
			return c;
		if (same(a,c))
			return b;
	}
	return NULL;
}

/*
 * When a CE gets turned into an unmerged entry, we
 * want it to be up-to-date
 */
static void verify_uptodate(struct cache_entry *ce)
{
	struct stat st;

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

/*
 * If the old tree contained a CE that isn't even in the
 * result, that's always a problem, regardless of whether
 * it's up-to-date or not (ie it can be a file that we
 * have updated but not committed yet).
 */
static void reject_merge(struct cache_entry *ce)
{
	die("Entry '%s' would be overwritten by merge. Cannot merge.", ce->name);
}

static int merged_entry_internal(struct cache_entry *merge, struct cache_entry *old, struct cache_entry **dst, int allow_dirty)
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
		} else if (!allow_dirty) {
			verify_uptodate(old);
		}
	}
	merge->ce_flags &= ~htons(CE_STAGEMASK);
	*dst++ = merge;
	return 1;
}

static int merged_entry_allow_dirty(struct cache_entry *merge, struct cache_entry *old, struct cache_entry **dst)
{
	return merged_entry_internal(merge, old, dst, 1);
}

static int merged_entry(struct cache_entry *merge, struct cache_entry *old, struct cache_entry **dst)
{
	return merged_entry_internal(merge, old, dst, 0);
}

static int deleted_entry(struct cache_entry *ce, struct cache_entry *old, struct cache_entry **dst)
{
	if (old)
		verify_uptodate(old);
	ce->ce_mode = 0;
	*dst++ = ce;
	return 1;
}

static int causes_df_conflict(struct cache_entry *ce, int stage,
			      struct cache_entry **dst_,
			      struct cache_entry **next_,
			      int tail)
{
	/* This is called during the merge operation and walking
	 * the active_cache[] array is messy, because it is in the
	 * middle of overlapping copy operation.  The invariants
	 * are:
	 * (1) active_cache points at the first (zeroth) entry.
	 * (2) up to dst pointer are resolved entries.
	 * (3) from the next pointer (head-inclusive) to the tail
	 *     of the active_cache array have the remaining paths
	 *     to be processed.  There can be a gap between dst
	 *     and next.  Note that next is called "src" in the
	 *     merge_cache() function, and tail is the original
	 *     end of active_cache array when merge_cache() started.
	 * (4) the path corresponding to *ce is not found in (2)
	 *     or (3).  It is in the gap.
	 *
	 *  active_cache -----......+++++++++++++.
	 *                    ^dst  ^next        ^tail
	 */
	int i, next, dst;
	const char *path = ce->name;
	int namelen = ce_namelen(ce);

	next = next_ - active_cache;
	dst = dst_ - active_cache;

	for (i = 0; i < tail; i++) {
		int entlen, len;
		const char *one, *two;
		if (dst <= i && i < next)
			continue;
		ce = active_cache[i];
		if (ce_stage(ce) != stage)
			continue;
		/* If ce->name is a prefix of path, then path is a file
		 * that hangs underneath ce->name, which is bad.
		 * If path is a prefix of ce->name, then it is the
		 * other way around which also is bad.
		 */
		entlen = ce_namelen(ce);
		if (namelen == entlen)
			continue;
		if (namelen < entlen) {
			len = namelen;
			one = path;
			two = ce->name;
		} else {
			len = entlen;
			one = ce->name;
			two = path;
		}
		if (memcmp(one, two, len))
			continue;
		if (two[len] == '/')
			return 1;
	}
	return 0;
}

static int threeway_merge(struct cache_entry *stages[4],
			  struct cache_entry **dst,
			  struct cache_entry **next, int tail)
{
	struct cache_entry *old = stages[0];
	struct cache_entry *a = stages[1], *b = stages[2], *c = stages[3];
	struct cache_entry *merge;
	int count;

	/* #5ALT */
	if (!a && b && c && same(b, c)) {
		if (old && !same(b, old))
			return -1;
		return merged_entry_allow_dirty(b, old, dst);
	}
	/* #2ALT and #3ALT */
	if (!a && (!!b != !!c)) {
		/*
		 * The reason we need to worry about directory/file
		 * conflicts only in #2ALT and #3ALT case is this:
		 *
		 * (1) For all other cases that read-tree internally
		 *     resolves a path, we always have such a path in
		 *     *both* stage2 and stage3 when we begin.
		 *     Traditionally, the behaviour has been even
		 *     stricter and we did not resolve a path without
		 *     initially being in all of stage1, 2, and 3.
		 *
		 * (2) When read-tree finishes, all resolved paths (i.e.
		 *     the paths that are in stage0) must have come from
		 *     either stage2 or stage3.  It is not possible to
		 *     have a stage0 path as a result of a merge if
		 *     neither stage2 nor stage3 had that path.
		 *
		 * (3) It is guaranteed that just after reading the
		 *     stages, each stage cannot have directory/file
		 *     conflicts on its own, because they are populated
		 *     by reading hierarchy of a tree.  Combined with
		 *     (1) and (2) above, this means that no matter what
		 *     combination of paths we take from stage2 and
		 *     stage3 as a result of a merge, they cannot cause
		 *     a directory/file conflict situation (otherwise
		 *     the "guilty" path would have already had such a
		 *     conflict in the original stage, either stage2
		 *     or stage3).  Although its stage2 is synthesized
		 *     by overlaying the current index on top of "our
		 *     head" tree, --emu23 case also has this guarantee,
		 *     by calling add_cache_entry() to create such stage2
		 *     entries.
		 *
		 * (4) Only #2ALT and #3ALT lack the guarantee (1).
		 *     They resolve paths that exist only in stage2
		 *     or stage3.  The stage2 tree may have a file DF
		 *     while stage3 tree may have a file DF/DF.  If
		 *     #2ALT and #3ALT rules happen to apply to both
		 *     of them, we would end up having DF (coming from
		 *     stage2) and DF/DF (from stage3) in the result.
		 *     When we attempt to resolve a path that exists
		 *     only in stage2, we need to make sure there is
		 *     no path that would conflict with it in stage3
		 *     and vice versa.
		 */
		if (c) { /* #2ALT */
			if (!causes_df_conflict(c, 2, dst, next, tail) &&
			    (!old || same(c, old)))
				return merged_entry_allow_dirty(c, old, dst);
		}
		else { /* #3ALT */
			if (!causes_df_conflict(b, 3, dst, next, tail) &&
			    (!old || same(b, old)))
				return merged_entry_allow_dirty(b, old, dst);
		}
		/* otherwise we will apply the original rule */
	}
	/* #14ALT */
	if (a && b && c && same(a, b) && !same(a, c)) {
		if (old && same(old, c))
			return merged_entry_allow_dirty(c, old, dst);
		/* otherwise the regular rule applies */
	}
	/*
	 * If we have an entry in the index cache ("old"), then we want
	 * to make sure that it matches any entries in stage 2 ("first
	 * branch", aka "b").
	 */
	if (old) {
		if (!b || !same(old, b))
			return -1;
	}
	merge = merge_entries(a, b, c);
	if (merge)
		return merged_entry(merge, old, dst);
	if (old)
		verify_uptodate(old);
	count = 0;
	if (a) { *dst++ = a; count++; }
	if (b) { *dst++ = b; count++; }
	if (c) { *dst++ = c; count++; }
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
static int twoway_merge(struct cache_entry **src, struct cache_entry **dst,
			struct cache_entry **next, int tail)
{
	struct cache_entry *current = src[0];
	struct cache_entry *oldtree = src[1], *newtree = src[2];

	if (src[3])
		return -1;

	if (current) {
		if ((!oldtree && !newtree) || /* 4 and 5 */
		    (!oldtree && newtree &&
		     same(current, newtree)) || /* 6 and 7 */
		    (oldtree && newtree &&
		     same(oldtree, newtree)) || /* 14 and 15 */
		    (oldtree && newtree &&
		     !same(oldtree, newtree) && /* 18 and 19*/
		     same(current, newtree))) {
			*dst++ = current;
			return 1;
		}
		else if (oldtree && !newtree && same(current, oldtree)) {
			/* 10 or 11 */
			return deleted_entry(oldtree, current, dst);
		}
		else if (oldtree && newtree &&
			 same(current, oldtree) && !same(current, newtree)) {
			/* 20 or 21 */
			return merged_entry(newtree, current, dst);
		}
		else
			/* all other failures */
			return -1;
	}
	else if (newtree)
		return merged_entry(newtree, current, dst);
	else
		return deleted_entry(oldtree, current, dst);
}

/*
 * Two-way merge emulated with three-way merge.
 *
 * This treats "read-tree -m H M" by transforming it internally
 * into "read-tree -m H I+H M", where I+H is a tree that would
 * contain the contents of the current index file, overlayed on
 * top of H.  Unlike the traditional two-way merge, this leaves
 * the stages in the resulting index file and lets the user resolve
 * the merge conflicts using standard tools for three-way merge.
 *
 * This function is just to set-up such an arrangement, and the
 * actual merge uses threeway_merge() function.
 */
static void setup_emu23(void)
{
	/* stage0 contains I, stage1 H, stage2 M.
	 * move stage2 to stage3, and create stage2 entries
	 * by scanning stage0 and stage1 entries.
	 */
	int i, namelen, size;
	struct cache_entry *ce, *stage2;

	for (i = 0; i < active_nr; i++) {
		ce = active_cache[i];
		if (ce_stage(ce) != 2)
			continue;
		/* hoist them up to stage 3 */
		namelen = ce_namelen(ce);
		ce->ce_flags = create_ce_flags(namelen, 3);
	}

	for (i = 0; i < active_nr; i++) {
		ce = active_cache[i];
		if (ce_stage(ce) > 1)
			continue;
		namelen = ce_namelen(ce);
		size = cache_entry_size(namelen);
		stage2 = xmalloc(size);
		memcpy(stage2, ce, size);
		stage2->ce_flags = create_ce_flags(namelen, 2);
		if (add_cache_entry(stage2, ADD_CACHE_OK_TO_ADD) < 0)
			die("cannot merge index and our head tree");

		/* We are done with this name, so skip to next name */
		while (i < active_nr &&
		       ce_namelen(active_cache[i]) == namelen &&
		       !memcmp(active_cache[i]->name, ce->name, namelen))
			i++;
		i--; /* compensate for the loop control */
	}
}

/*
 * One-way merge.
 *
 * The rule is:
 * - take the stat information from stage0, take the data from stage1
 */
static int oneway_merge(struct cache_entry **src, struct cache_entry **dst,
			struct cache_entry **next, int tail)
{
	struct cache_entry *old = src[0];
	struct cache_entry *a = src[1];

	if (src[2] || src[3])
		return -1;

	if (!a)
		return 0;
	if (old && same(old, a)) {
		*dst++ = old;
		return 1;
	}
	return merged_entry(a, NULL, dst);
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
				unlink(ce->name);
			continue;
		}
		if (ce->ce_flags & mask) {
			ce->ce_flags &= ~mask;
			if (update)
				checkout_entry(ce, &state);
		}
	}
}

typedef int (*merge_fn_t)(struct cache_entry **, struct cache_entry **, struct cache_entry **, int);

static void merge_cache(struct cache_entry **src, int nr, merge_fn_t fn)
{
	struct cache_entry **dst = src;
	int tail = nr;

	while (nr) {
		int entries;
		struct cache_entry *name, *ce, *stages[4] = { NULL, };

		name = ce = *src;
		for (;;) {
			int stage = ce_stage(ce);
			stages[stage] = ce;
			ce = *++src;
			active_nr--;
			if (!--nr)
				break;
			if (!path_matches(ce, name))
				break;
		}

		entries = fn(stages, dst, src, tail);
		if (entries < 0)
			reject_merge(name);
		dst += entries;
		active_nr += entries;
	}
	check_updates(active_cache, active_nr);
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

static char *read_tree_usage = "git-read-tree (<sha> | -m [-u] <sha1> [<sha2> [<sha3>]])";

static struct cache_file cache_file;

int main(int argc, char **argv)
{
	int i, newfd, merge, reset, emu23;
	unsigned char sha1[20];

	newfd = hold_index_file_for_update(&cache_file, get_index_file());
	if (newfd < 0)
		die("unable to create new cachefile");

	merge = 0;
	reset = 0;
	emu23 = 0;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		/* "-u" means "update", meaning that a merge will update the working directory */
		if (!strcmp(arg, "-u")) {
			update = 1;
			continue;
		}

		/* This differs from "-m" in that we'll silently ignore unmerged entries */
		if (!strcmp(arg, "--reset")) {
			if (stage || merge || emu23)
				usage(read_tree_usage);
			reset = 1;
			merge = 1;
			stage = 1;
			read_cache_unmerged();
			continue;
		}

		/* "-m" stands for "merge", meaning we start in stage 1 */
		if (!strcmp(arg, "-m")) {
			if (stage || merge || emu23)
				usage(read_tree_usage);
			if (read_cache_unmerged())
				die("you need to resolve your current index first");
			stage = 1;
			merge = 1;
			continue;
		}

		/* "-emu23" uses 3-way merge logic to perform fast-forward */
		if (!strcmp(arg, "--emu23")) {
			if (stage || merge || emu23)
				usage(read_tree_usage);
			if (read_cache_unmerged())
				die("you need to resolve your current index first");
			merge = emu23 = stage = 1;
			continue;
		}

		if (get_sha1(arg, sha1) < 0)
			usage(read_tree_usage);
		if (stage > 3)
			usage(read_tree_usage);
		if (unpack_tree(sha1) < 0)
			die("failed to unpack tree object %s", arg);
		stage++;
	}
	if (update && !merge)
		usage(read_tree_usage);
	if (merge) {
		static const merge_fn_t merge_function[] = {
			[1] = oneway_merge,
			[2] = twoway_merge,
			[3] = threeway_merge,
		};
		merge_fn_t fn;

		if (stage < 2 || stage > 4)
			die("just how do you expect me to merge %d trees?", stage-1);
		if (emu23 && stage != 3)
			die("--emu23 takes only two trees");
		fn = merge_function[stage-1];
		if (stage == 3 && emu23) { 
			setup_emu23();
			fn = merge_function[3];
		}
		merge_cache(active_cache, active_nr, fn);
	}
	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_index_file(&cache_file))
		die("unable to write new index file");
	return 0;
}
