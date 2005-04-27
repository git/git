#include "cache.h"

static int cached_only = 0;
static int line_termination = '\n';

/* A file entry went away or appeared */
static void show_file(const char *prefix, struct cache_entry *ce)
{
	printf("%s%o\t%s\t%s\t%s%c", prefix, ntohl(ce->ce_mode), "blob",
	       sha1_to_hex(ce->sha1), ce->name, line_termination);
}

static int show_modified(struct cache_entry *old, struct cache_entry *new)
{
	unsigned int mode = ntohl(new->ce_mode), oldmode;
	unsigned char *sha1 = new->sha1;
	unsigned char old_sha1_hex[60];

	if (!cached_only) {
		static unsigned char no_sha1[20];
		int changed;
		struct stat st;
		if (stat(new->name, &st) < 0) {
			show_file("-", old);
			return -1;
		}
		changed = cache_match_stat(new, &st);
		if (changed) {
			mode = st.st_mode;
			sha1 = no_sha1;
		}
	}

	oldmode = ntohl(old->ce_mode);
	if (mode == oldmode && !memcmp(sha1, old->sha1, 20))
		return 0;

	strcpy(old_sha1_hex, sha1_to_hex(old->sha1));
	printf("*%o->%o\t%s\t%s->%s\t%s%c", oldmode, mode,
	       "blob",
	       old_sha1_hex, sha1_to_hex(sha1),
	       old->name, line_termination);
	return 0;
}

static int diff_cache(struct cache_entry **ac, int entries)
{
	while (entries) {
		struct cache_entry *ce = *ac;
		int same = (entries > 1) && same_name(ce, ac[1]);

		switch (ce_stage(ce)) {
		case 0:
			/* No stage 1 entry? That means it's a new file */
			if (!same) {
				show_file("+", ce);
				break;
			}
			/* Show difference between old and new */
			show_modified(ac[1], ce);
			break;
		case 1:
			/* No stage 3 (merge) entry? That means it's been deleted */
			if (!same) {
				show_file("-", ce);
				break;
			}
			/* Otherwise we fall through to the "unmerged" case */
		case 3:
			printf("U %s%c", ce->name, line_termination);
			break;

		default:
			die("impossible cache entry stage");
		}

		/*
		 * Ignore all the different stages for this file,
		 * we've handled the relevant cases now.
		 */
		do {
			ac++;
			entries--;
		} while (entries && same_name(ce, ac[0]));
	}
	return 0;
}

/*
 * This turns all merge entries into "stage 3". That guarantees that
 * when we read in the new tree (into "stage 1"), we won't lose sight
 * of the fact that we had unmerged entries.
 */
static void mark_merge_entries(void)
{
	int i;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (!ce_stage(ce))
			continue;
		ce->ce_flags |= htons(CE_STAGEMASK);
	}
}

static char *diff_cache_usage = "diff-cache [-r] [-z] [--cached] <tree sha1>";

int main(int argc, char **argv)
{
	unsigned char tree_sha1[20];
	void *tree;
	unsigned long size;

	read_cache();
	while (argc > 2) {
		char *arg = argv[1];
		argv++;
		argc--;
		if (!strcmp(arg, "-r")) {
			/* We accept the -r flag just to look like diff-tree */
			continue;
		}
		if (!strcmp(arg, "-z")) {
			line_termination = '\0';
			continue;
		}
		if (!strcmp(arg, "--cached")) {
			cached_only = 1;
			continue;
		}
		usage(diff_cache_usage);
	}

	if (argc != 2 || get_sha1_hex(argv[1], tree_sha1))
		usage(diff_cache_usage);

	mark_merge_entries();

	tree = read_tree_with_tree_or_commit_sha1(tree_sha1, &size, 0);
	if (!tree)
		die("bad tree object %s", argv[1]);
	if (read_tree(tree, size, 1))
		die("unable to read tree object %s", argv[1]);

	return diff_cache(active_cache, active_nr);
}
