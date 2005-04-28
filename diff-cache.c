#include "cache.h"
#include "diff.h"

static int cached_only = 0;
static int generate_patch = 0;
static int line_termination = '\n';

/* A file entry went away or appeared */
static void show_file(const char *prefix, struct cache_entry *ce, unsigned char *sha1, unsigned int mode)
{
	if (generate_patch)
		diff_addremove(prefix[0], ntohl(mode), sha1, ce->name, NULL);
	else
		printf("%s%06o\tblob\t%s\t%s%c", prefix, ntohl(mode),
		       sha1_to_hex(sha1), ce->name, line_termination);
}

static int get_stat_data(struct cache_entry *ce, unsigned char **sha1p, unsigned int *modep)
{
	unsigned char *sha1 = ce->sha1;
	unsigned int mode = ce->ce_mode;

	if (!cached_only) {
		static unsigned char no_sha1[20];
		int changed;
		struct stat st;
		if (stat(ce->name, &st) < 0)
			return -1;
		changed = cache_match_stat(ce, &st);
		if (changed) {
			mode = create_ce_mode(st.st_mode);
			sha1 = no_sha1;
		}
	}

	*sha1p = sha1;
	*modep = mode;
	return 0;
}

static void show_new_file(struct cache_entry *new)
{
	unsigned char *sha1;
	unsigned int mode;

	/* New file in the index: it might actually be different in the working copy */
	if (get_stat_data(new, &sha1, &mode) < 0)
		return;

	show_file("+", new, sha1, mode);
}

static int show_modified(struct cache_entry *old, struct cache_entry *new)
{
	unsigned int mode, oldmode;
	unsigned char *sha1;
	unsigned char old_sha1_hex[60];

	if (get_stat_data(new, &sha1, &mode) < 0) {
		show_file("-", old, old->sha1, old->ce_mode);
		return -1;
	}

	oldmode = old->ce_mode;
	if (mode == oldmode && !memcmp(sha1, old->sha1, 20))
		return 0;

	mode = ntohl(mode);
	oldmode = ntohl(oldmode);

	if (generate_patch)
		diff_change(oldmode, mode,
			    old->sha1, sha1, old->name, NULL);
	else {
		strcpy(old_sha1_hex, sha1_to_hex(old->sha1));
		printf("*%06o->%06o\tblob\t%s->%s\t%s%c", oldmode, mode,
		       old_sha1_hex, sha1_to_hex(sha1),
		       old->name, line_termination);
	}
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
				show_new_file(ce);
				break;
			}
			/* Show difference between old and new */
			show_modified(ac[1], ce);
			break;
		case 1:
			/* No stage 3 (merge) entry? That means it's been deleted */
			if (!same) {
				show_file("-", ce, ce->sha1, ce->ce_mode);
				break;
			}
			/* Otherwise we fall through to the "unmerged" case */
		case 3:
			if (generate_patch)
				diff_unmerge(ce->name);
			else
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

static char *diff_cache_usage =
"diff-cache [-r] [-z] [-p] [--cached] <tree sha1>";

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
		if (!strcmp(arg, "-p")) {
			generate_patch = 1;
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

	tree = read_object_with_reference(tree_sha1, "tree", &size, 0);
	if (!tree)
		die("bad tree object %s", argv[1]);
	if (read_tree(tree, size, 1))
		die("unable to read tree object %s", argv[1]);

	return diff_cache(active_cache, active_nr);
}
