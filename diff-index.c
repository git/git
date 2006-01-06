#include "cache.h"
#include "diff.h"

static int cached_only = 0;
static int match_nonexisting = 0;
static struct diff_options diff_options;

/* A file entry went away or appeared */
static void show_file(const char *prefix,
		      struct cache_entry *ce,
		      unsigned char *sha1, unsigned int mode)
{
	diff_addremove(&diff_options, prefix[0], ntohl(mode),
		       sha1, ce->name, NULL);
}

static int get_stat_data(struct cache_entry *ce,
			 unsigned char ** sha1p, unsigned int *modep)
{
	unsigned char *sha1 = ce->sha1;
	unsigned int mode = ce->ce_mode;

	if (!cached_only) {
		static unsigned char no_sha1[20];
		int changed;
		struct stat st;
		if (lstat(ce->name, &st) < 0) {
			if (errno == ENOENT && match_nonexisting) {
				*sha1p = sha1;
				*modep = mode;
				return 0;
			}
			return -1;
		}
		changed = ce_match_stat(ce, &st);
		if (changed) {
			mode = create_ce_mode(st.st_mode);
			if (!trust_executable_bit &&
			    S_ISREG(mode) && S_ISREG(ce->ce_mode) &&
			    ((mode ^ ce->ce_mode) == 0111))
				mode = ce->ce_mode;
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

	/* New file in the index: it might actually be different in
	 * the working copy.
	 */
	if (get_stat_data(new, &sha1, &mode) < 0)
		return;

	show_file("+", new, sha1, mode);
}

static int show_modified(struct cache_entry *old,
			 struct cache_entry *new,
			 int report_missing)
{
	unsigned int mode, oldmode;
	unsigned char *sha1;

	if (get_stat_data(new, &sha1, &mode) < 0) {
		if (report_missing)
			show_file("-", old, old->sha1, old->ce_mode);
		return -1;
	}

	oldmode = old->ce_mode;
	if (mode == oldmode && !memcmp(sha1, old->sha1, 20) &&
	    !diff_options.find_copies_harder)
		return 0;

	mode = ntohl(mode);
	oldmode = ntohl(oldmode);

	diff_change(&diff_options, oldmode, mode,
		    old->sha1, sha1, old->name, NULL);
	return 0;
}

static int diff_cache(struct cache_entry **ac, int entries, const char **pathspec)
{
	while (entries) {
		struct cache_entry *ce = *ac;
		int same = (entries > 1) && ce_same_name(ce, ac[1]);

		if (!ce_path_match(ce, pathspec))
			goto skip_entry;

		switch (ce_stage(ce)) {
		case 0:
			/* No stage 1 entry? That means it's a new file */
			if (!same) {
				show_new_file(ce);
				break;
			}
			/* Show difference between old and new */
			show_modified(ac[1], ce, 1);
			break;
		case 1:
			/* No stage 3 (merge) entry? That means it's been deleted */
			if (!same) {
				show_file("-", ce, ce->sha1, ce->ce_mode);
				break;
			}
			/* We come here with ce pointing at stage 1
			 * (original tree) and ac[1] pointing at stage
			 * 3 (unmerged).  show-modified with
			 * report-missing set to false does not say the
			 * file is deleted but reports true if work
			 * tree does not have it, in which case we
			 * fall through to report the unmerged state.
			 * Otherwise, we show the differences between
			 * the original tree and the work tree.
			 */
			if (!cached_only && !show_modified(ce, ac[1], 0))
				break;
			/* fallthru */
		case 3:
			diff_unmerge(&diff_options, ce->name);
			break;

		default:
			die("impossible cache entry stage");
		}

skip_entry:
		/*
		 * Ignore all the different stages for this file,
		 * we've handled the relevant cases now.
		 */
		do {
			ac++;
			entries--;
		} while (entries && ce_same_name(ce, ac[0]));
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

static const char diff_cache_usage[] =
"git-diff-index [-m] [--cached] "
"[<common diff options>] <tree-ish> [<path>...]"
COMMON_DIFF_OPTIONS_HELP;

int main(int argc, const char **argv)
{
	const char *tree_name = NULL;
	unsigned char sha1[20];
	const char *prefix = setup_git_directory();
	const char **pathspec = NULL;
	void *tree;
	unsigned long size;
	int ret;
	int allow_options = 1;
	int i;

	git_config(git_diff_config);
	diff_setup(&diff_options);
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		int diff_opt_cnt;

		if (!allow_options || *arg != '-') {
			if (tree_name)
				break;
			tree_name = arg;
			continue;
		}
			
		if (!strcmp(arg, "--")) {
			allow_options = 0;
			continue;
		}
		if (!strcmp(arg, "-r")) {
			/* We accept the -r flag just to look like git-diff-tree */
			continue;
		}
		diff_opt_cnt = diff_opt_parse(&diff_options, argv + i,
					      argc - i);
		if (diff_opt_cnt < 0)
			usage(diff_cache_usage);
		else if (diff_opt_cnt) {
			i += diff_opt_cnt - 1;
			continue;
		}

		if (!strcmp(arg, "-m")) {
			match_nonexisting = 1;
			continue;
		}
		if (!strcmp(arg, "--cached")) {
			cached_only = 1;
			continue;
		}
		usage(diff_cache_usage);
	}

	pathspec = get_pathspec(prefix, argv + i);

	if (diff_setup_done(&diff_options) < 0)
		usage(diff_cache_usage);

	if (!tree_name || get_sha1(tree_name, sha1))
		usage(diff_cache_usage);

	read_cache();

	mark_merge_entries();

	tree = read_object_with_reference(sha1, "tree", &size, NULL);
	if (!tree)
		die("bad tree object %s", tree_name);
	if (read_tree(tree, size, 1, pathspec))
		die("unable to read tree object %s", tree_name);

	ret = diff_cache(active_cache, active_nr, pathspec);

	diffcore_std(&diff_options);
	diff_flush(&diff_options);
	return ret;
}
