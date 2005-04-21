/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static int stage = 0;

static int read_one_entry(unsigned char *sha1, const char *base, int baselen, const char *pathname, unsigned mode)
{
	int len = strlen(pathname);
	unsigned int size = cache_entry_size(baselen + len);
	struct cache_entry *ce = malloc(size);

	memset(ce, 0, size);

	ce->ce_mode = create_ce_mode(mode);
	ce->ce_flags = create_ce_flags(baselen + len, stage);
	memcpy(ce->name, base, baselen);
	memcpy(ce->name + baselen, pathname, len+1);
	memcpy(ce->sha1, sha1, 20);
	return add_cache_entry(ce, 1);
}

static int read_tree_recursive(void *buffer, const char *type,
			       unsigned long size,
			       const char *base, int baselen)
{
	if (!buffer || strcmp(type, "tree"))
		return -1;
	while (size) {
		int len = strlen(buffer)+1;
		unsigned char *sha1 = buffer + len;
		char *path = strchr(buffer, ' ')+1;
		unsigned int mode;

		if (size < len + 20 || sscanf(buffer, "%o", &mode) != 1)
			return -1;

		buffer = sha1 + 20;
		size -= len + 20;

		if (S_ISDIR(mode)) {
			int retval;
			int pathlen = strlen(path);
			char *newbase = malloc(baselen + 1 + pathlen);
			void *eltbuf;
			char elttype[20];
			unsigned long eltsize;

			eltbuf = read_sha1_file(sha1, elttype, &eltsize);
			if (!eltbuf)
				return -1;
			memcpy(newbase, base, baselen);
			memcpy(newbase + baselen, path, pathlen);
			newbase[baselen + pathlen] = '/';
			retval = read_tree_recursive(eltbuf, elttype, eltsize,
						     newbase,
						     baselen + pathlen + 1);
			free(eltbuf);
			free(newbase);
			if (retval)
				return -1;
			continue;
		}
		if (read_one_entry(sha1, base, baselen, path, mode) < 0)
			return -1;
	}
	return 0;
}

static int read_tree(unsigned char *sha1, const char *base, int baselen)
{
	void *buffer;
	unsigned long size;

	buffer = read_tree_with_tree_or_commit_sha1(sha1, &size, 0);
	return read_tree_recursive(buffer, "tree", size, base, baselen);
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

static void trivially_merge_cache(struct cache_entry **src, int nr)
{
	static struct cache_entry null_entry;
	struct cache_entry **dst = src;
	struct cache_entry *old = &null_entry;

	while (nr) {
		struct cache_entry *ce, *result;

		ce = src[0];

		/* We throw away original cache entries except for the stat information */
		if (!ce_stage(ce)) {
			old = ce;
			src++;
			nr--;
			active_nr--;
			continue;
		}
		if (nr > 2 && (result = merge_entries(ce, src[1], src[2])) != NULL) {
			/*
			 * See if we can re-use the old CE directly?
			 * That way we get the uptodate stat info.
			 */
			if (path_matches(result, old) && same(result, old))
				*result = *old;
			ce = result;
			ce->ce_flags &= ~htons(CE_STAGEMASK);
			src += 2;
			nr -= 2;
			active_nr -= 2;
		}
		*dst++ = ce;
		src++;
		nr--;
	}
}

static void merge_stat_info(struct cache_entry **src, int nr)
{
	static struct cache_entry null_entry;
	struct cache_entry **dst = src;
	struct cache_entry *old = &null_entry;

	while (nr) {
		struct cache_entry *ce;

		ce = src[0];

		/* We throw away original cache entries except for the stat information */
		if (!ce_stage(ce)) {
			old = ce;
			src++;
			nr--;
			active_nr--;
			continue;
		}
		if (path_matches(ce, old) && same(ce, old))
			*ce = *old;
		ce->ce_flags &= ~htons(CE_STAGEMASK);
		*dst++ = ce;
		src++;
		nr--;
	}
}

static char *read_tree_usage = "read-tree (<sha> | -m <sha1> [<sha2> <sha3>])";

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
		if (get_sha1_hex(arg, sha1) < 0)
			usage(read_tree_usage);
		if (stage > 3)
			usage(read_tree_usage);
		if (read_tree(sha1, "", 0) < 0)
			die("failed to unpack tree object %s", arg);
		stage++;
	}
	if (merge) {
		switch (stage) {
		case 4:	/* Three-way merge */
			trivially_merge_cache(active_cache, active_nr);
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
