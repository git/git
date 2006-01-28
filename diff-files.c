/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "diff.h"

static const char diff_files_usage[] =
"git-diff-files [-q] [-0/-1/2/3 |-c|--cc] [<common diff options>] [<path>...]"
COMMON_DIFF_OPTIONS_HELP;

static struct diff_options diff_options;
static int silent = 0;
static int diff_unmerged_stage = 2;
static int combine_merges = 0;
static int dense_combined_merges = 0;

static void show_unmerge(const char *path)
{
	diff_unmerge(&diff_options, path);
}

static void show_file(int pfx, struct cache_entry *ce)
{
	diff_addremove(&diff_options, pfx, ntohl(ce->ce_mode),
		       ce->sha1, ce->name, NULL);
}

static void show_modified(int oldmode, int mode,
			  const unsigned char *old_sha1, const unsigned char *sha1,
			  char *path)
{
	diff_change(&diff_options, oldmode, mode, old_sha1, sha1, path, NULL);
}

int main(int argc, const char **argv)
{
	const char **pathspec;
	const char *prefix = setup_git_directory();
	int entries, i;

	git_config(git_diff_config);
	diff_setup(&diff_options);
	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "--")) {
			argv++;
			argc--;
			break;
		}
		if (!strcmp(argv[1], "-0"))
			diff_unmerged_stage = 0;
		else if (!strcmp(argv[1], "-1"))
			diff_unmerged_stage = 1;
		else if (!strcmp(argv[1], "-2"))
			diff_unmerged_stage = 2;
		else if (!strcmp(argv[1], "-3"))
			diff_unmerged_stage = 3;
		else if (!strcmp(argv[1], "--base"))
			diff_unmerged_stage = 1;
		else if (!strcmp(argv[1], "--ours"))
			diff_unmerged_stage = 2;
		else if (!strcmp(argv[1], "--theirs"))
			diff_unmerged_stage = 3;
		else if (!strcmp(argv[1], "-q"))
			silent = 1;
		else if (!strcmp(argv[1], "-r"))
			; /* no-op */
		else if (!strcmp(argv[1], "-s"))
			; /* no-op */
		else if (!strcmp(argv[1], "-c"))
			combine_merges = 1;
		else if (!strcmp(argv[1], "--cc"))
			dense_combined_merges = combine_merges = 1;
		else {
			int diff_opt_cnt;
			diff_opt_cnt = diff_opt_parse(&diff_options,
						      argv+1, argc-1);
			if (diff_opt_cnt < 0)
				usage(diff_files_usage);
			else if (diff_opt_cnt) {
				argv += diff_opt_cnt;
				argc -= diff_opt_cnt;
				continue;
			}
			else
				usage(diff_files_usage);
		}
		argv++; argc--;
	}
	if (combine_merges) {
		diff_options.output_format = DIFF_FORMAT_PATCH;
	}

	/* Find the directory, and set up the pathspec */
	pathspec = get_pathspec(prefix, argv + 1);
	entries = read_cache();

	if (diff_setup_done(&diff_options) < 0)
		usage(diff_files_usage);

	/* At this point, if argc == 1, then we are doing everything.
	 * Otherwise argv[1] .. argv[argc-1] have the explicit paths.
	 */
	if (entries < 0) {
		perror("read_cache");
		exit(1);
	}

	for (i = 0; i < entries; i++) {
		struct stat st;
		unsigned int oldmode, newmode;
		struct cache_entry *ce = active_cache[i];
		int changed;

		if (!ce_path_match(ce, pathspec))
			continue;

		if (ce_stage(ce)) {
			struct {
				struct combine_diff_path p;
				unsigned char fill[4][20];
			} combine;
			int num_compare_stages = 0;

			combine.p.next = NULL;
			combine.p.len = ce_namelen(ce);
			combine.p.path = xmalloc(combine.p.len + 1);
			memcpy(combine.p.path, ce->name, combine.p.len);
			combine.p.path[combine.p.len] = 0;
			memset(combine.p.sha1, 0, 100);

			while (i < entries) {
				struct cache_entry *nce = active_cache[i];
				int stage;

				if (strcmp(ce->name, nce->name))
					break;

				/* Stage #2 (ours) is the first parent,
				 * stage #3 (theirs) is the second.
				 */
				stage = ce_stage(nce);
				if (2 <= stage) {
					num_compare_stages++;
					memcpy(combine.p.parent_sha1[stage-2],
					       nce->sha1, 20);
				}

				/* diff against the proper unmerged stage */
				if (stage == diff_unmerged_stage)
					ce = nce;
				i++;
			}
			/*
			 * Compensate for loop update
			 */
			i--;

			if (combine_merges && num_compare_stages == 2) {
				show_combined_diff(&combine.p, 2,
						   dense_combined_merges,
						   NULL, 0);
				free(combine.p.path);
				continue;
			}
			free(combine.p.path);

			/*
			 * Show the diff for the 'ce' if we found the one
			 * from the desired stage.
			 */
			show_unmerge(ce->name);
			if (ce_stage(ce) != diff_unmerged_stage)
				continue;
		}

		if (lstat(ce->name, &st) < 0) {
			if (errno != ENOENT && errno != ENOTDIR) {
				perror(ce->name);
				continue;
			}
			if (silent)
				continue;
			show_file('-', ce);
			continue;
		}
		changed = ce_match_stat(ce, &st);
		if (!changed && !diff_options.find_copies_harder)
			continue;
		oldmode = ntohl(ce->ce_mode);

		newmode = DIFF_FILE_CANON_MODE(st.st_mode);
		if (!trust_executable_bit &&
		    S_ISREG(newmode) && S_ISREG(oldmode) &&
		    ((newmode ^ oldmode) == 0111))
			newmode = oldmode;
		show_modified(oldmode, newmode,
			      ce->sha1, (changed ? null_sha1 : ce->sha1),
			      ce->name);
	}
	diffcore_std(&diff_options);
	diff_flush(&diff_options);
	return 0;
}
