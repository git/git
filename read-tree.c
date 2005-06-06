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

static char *lockfile_name;

static void remove_lock_file(void)
{
	if (lockfile_name)
		unlink(lockfile_name);
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
 *
 * _Any_ other merge is left to user policy.  That includes "both
 * created the same file", and "both removed the same file" - which are
 * trivial, but the user might still want to _note_ it. 
 */
static struct cache_entry *merge_entries(struct cache_entry *a,
					 struct cache_entry *b,
					 struct cache_entry *c)
{
	int len = ce_namelen(a);

	/*
	 * Are they all the same filename? We won't do
	 * any name merging
	 */
	if (ce_namelen(b) != len ||
	    ce_namelen(c) != len ||
	    memcmp(a->name, b->name, len) ||
	    memcmp(a->name, c->name, len))
		return NULL;

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
	if (same(b,c))
		return c;
	if (same(a,b))
		return c;
	if (same(a,c))
		return b;
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

#define CHECK_OLD(ce) if (old && same(old, ce)) { verify_uptodate(old); old = NULL; }

static void trivially_merge_cache(struct cache_entry **src, int nr)
{
	struct cache_entry **dst = src;
	struct cache_entry *old = NULL;

	while (nr--) {
		struct cache_entry *ce, *result;

		ce = *src++;

		/* We throw away original cache entries except for the stat information */
		if (!ce_stage(ce)) {
			if (old)
				reject_merge(old);
			old = ce;
			active_nr--;
			continue;
		}
		if (old && !path_matches(old, ce))
			reject_merge(old);
		if (nr > 1 && (result = merge_entries(ce, src[0], src[1])) != NULL) {
			result->ce_flags |= htons(CE_UPDATE);
			/*
			 * See if we can re-use the old CE directly?
			 * That way we get the uptodate stat info.
			 *
			 * This also removes the UPDATE flag on
			 * a match.
			 */
			if (old && same(old, result)) {
				*result = *old;
				old = NULL;
			}
			CHECK_OLD(ce);
			CHECK_OLD(src[0]);
			CHECK_OLD(src[1]);
			ce = result;
			ce->ce_flags &= ~htons(CE_STAGEMASK);
			src += 2;
			nr -= 2;
			active_nr -= 2;
		}

		/*
		 * If we had an old entry that we now effectively
		 * overwrite, make sure it wasn't dirty.
		 */
		CHECK_OLD(ce);
		*dst++ = ce;
	}
	if (old)
		reject_merge(old);
}

/*
 * When we find a "stage2" entry in the two-way merge, that's
 * the one that will remain. If we have an exact old match,
 * we don't care whether the file is up-to-date or not, we just
 * re-use the thing directly.
 *
 * If we didn't have an exact match, then we want to make sure
 * that we've seen a stage1 that matched the old, and that the
 * old file was up-to-date. Because it will be gone after this
 * merge..
 */
static void twoway_check(struct cache_entry *old, int seen_stage1, struct cache_entry *ce)
{
	if (path_matches(old, ce)) {
		/*
		 * This also removes the UPDATE flag on
		 * a match
		 */
		if (same(old, ce)) {
			*ce = *old;
			return;
		}
		if (!seen_stage1)
			reject_merge(old);
	}
	verify_uptodate(old);
}

/*
 * Two-way merge.
 *
 * The rule is: 
 *  - every current entry has to match the old tree
 *  - if the current entry matches the new tree, we leave it
 *    as-is. Otherwise we require that it be up-to-date.
 */
static void twoway_merge(struct cache_entry **src, int nr)
{
	int seen_stage1 = 0;
	struct cache_entry *old = NULL;
	struct cache_entry **dst = src;

	while (nr--) {
		struct cache_entry *ce = *src++;
		int stage = ce_stage(ce);

		switch (stage) {
		case 0:
			if (old)
				reject_merge(old);
			old = ce;
			seen_stage1 = 0;
			active_nr--;
			continue;

		case 1:
			active_nr--;
			if (!old)
				continue;
			if (!path_matches(old, ce) || !same(old, ce))
				reject_merge(old);
			seen_stage1 = 1;
			continue;

		case 2:
			ce->ce_flags |= htons(CE_UPDATE);
			if (old) {
				twoway_check(old, seen_stage1, ce);
				old = NULL;
			}
			ce->ce_flags &= ~htons(CE_STAGEMASK);
			*dst++ = ce;
			continue;
		}
		die("impossible two-way stage");
	}

	/*
	 * Unmatched with a new entry? Make sure it was
	 * at least uptodate in the working directory _and_
	 * the original tree..
	 */
	if (old) {
		if (!seen_stage1)
			reject_merge(old);
		verify_uptodate(old);
	}
}

static void merge_stat_info(struct cache_entry **src, int nr)
{
	static struct cache_entry null_entry;
	struct cache_entry **dst = src;
	struct cache_entry *stat = &null_entry;

	while (nr--) {
		struct cache_entry *ce = *src++;

		/* We throw away original cache entries except for the stat information */
		if (!ce_stage(ce)) {
			stat = ce;
			active_nr--;
			continue;
		}
		if (path_matches(ce, stat) && same(ce, stat))
			*ce = *stat;
		ce->ce_flags &= ~htons(CE_STAGEMASK);
		*dst++ = ce;
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
		if (ce->ce_flags & mask) {
			ce->ce_flags &= ~mask;
			if (update)
				checkout_entry(ce, &state);
		}
	}
}

static char *read_tree_usage = "git-read-tree (<sha> | -m <sha1> [<sha2> [<sha3>]])";

int main(int argc, char **argv)
{
	int i, newfd, merge;
	unsigned char sha1[20];
	static char lockfile[MAXPATHLEN+1];
	const char *indexfile = get_index_file();

	snprintf(lockfile, sizeof(lockfile), "%s.lock", indexfile);

	newfd = open(lockfile, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (newfd < 0)
		die("unable to create new cachefile");
	atexit(remove_lock_file);
	lockfile_name = lockfile;

	merge = 0;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		/* "-u" means "update", meaning that a merge will update the working directory */
		if (!strcmp(arg, "-u")) {
			update = 1;
			continue;
		}

		/* "-m" stands for "merge", meaning we start in stage 1 */
		if (!strcmp(arg, "-m")) {
			int i;
			if (stage)
				die("-m needs to come first");
			read_cache();
			for (i = 0; i < active_nr; i++) {
				if (ce_stage(active_cache[i]))
					die("you need to resolve your current index first");
			}
			stage = 1;
			merge = 1;
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
	if (merge) {
		switch (stage) {
		case 4:	/* Three-way merge */
			trivially_merge_cache(active_cache, active_nr);
			check_updates(active_cache, active_nr);
			break;
		case 3:	/* Update from one tree to another */
			twoway_merge(active_cache, active_nr);
			check_updates(active_cache, active_nr);
			break;
		case 2:	/* Just read a tree, merge with old cache contents */
			merge_stat_info(active_cache, active_nr);
			break;
		default:
			die("just how do you expect me to merge %d trees?", stage-1);
		}
	}
	if (write_cache(newfd, active_cache, active_nr) || rename(lockfile, indexfile))
		die("unable to write new index file");
	lockfile_name = NULL;
	return 0;
}
