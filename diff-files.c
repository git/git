/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "diff.h"

static const char diff_files_usage[] =
"git-diff-files [-q] "
"[<common diff options>] [<path>...]"
COMMON_DIFF_OPTIONS_HELP;

static int diff_output_format = DIFF_FORMAT_RAW;
static int diff_line_termination = '\n';
static int detect_rename = 0;
static int find_copies_harder = 0;
static int diff_setup_opt = 0;
static int diff_score_opt = 0;
static const char *pickaxe = NULL;
static int pickaxe_opts = 0;
static int diff_break_opt = -1;
static const char *orderfile = NULL;
static const char *diff_filter = NULL;
static int silent = 0;

static void show_unmerge(const char *path)
{
	diff_unmerge(path);
}

static void show_file(int pfx, struct cache_entry *ce)
{
	diff_addremove(pfx, ntohl(ce->ce_mode), ce->sha1, ce->name, NULL);
}

static void show_modified(int oldmode, int mode,
			  const unsigned char *old_sha1, const unsigned char *sha1,
			  char *path)
{
	diff_change(oldmode, mode, old_sha1, sha1, path, NULL);
}

int main(int argc, const char **argv)
{
	static const unsigned char null_sha1[20] = { 0, };
	const char **pathspec;
	int entries = read_cache();
	int i;

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-p") || !strcmp(argv[1], "-u"))
			diff_output_format = DIFF_FORMAT_PATCH;
		else if (!strcmp(argv[1], "-q"))
			silent = 1;
		else if (!strcmp(argv[1], "-r"))
			; /* no-op */
		else if (!strcmp(argv[1], "-s"))
			; /* no-op */
		else if (!strcmp(argv[1], "-z"))
			diff_line_termination = 0;
		else if (!strcmp(argv[1], "--name-only"))
			diff_output_format = DIFF_FORMAT_NAME;
		else if (!strcmp(argv[1], "-R"))
			diff_setup_opt |= DIFF_SETUP_REVERSE;
		else if (!strncmp(argv[1], "-S", 2))
			pickaxe = argv[1] + 2;
		else if (!strncmp(argv[1], "-O", 2))
			orderfile = argv[1] + 2;
		else if (!strncmp(argv[1], "--diff-filter=", 14))
			diff_filter = argv[1] + 14;
		else if (!strcmp(argv[1], "--pickaxe-all"))
			pickaxe_opts = DIFF_PICKAXE_ALL;
		else if (!strncmp(argv[1], "-B", 2)) {
			if ((diff_break_opt =
			     diff_scoreopt_parse(argv[1])) == -1)
				usage(diff_files_usage);
		}
		else if (!strncmp(argv[1], "-M", 2)) {
			if ((diff_score_opt =
			     diff_scoreopt_parse(argv[1])) == -1)
				usage(diff_files_usage);
			detect_rename = DIFF_DETECT_RENAME;
		}
		else if (!strncmp(argv[1], "-C", 2)) {
			if ((diff_score_opt =
			     diff_scoreopt_parse(argv[1])) == -1)
				usage(diff_files_usage);
			detect_rename = DIFF_DETECT_COPY;
		}
		else if (!strcmp(argv[1], "--find-copies-harder"))
			find_copies_harder = 1;
		else
			usage(diff_files_usage);
		argv++; argc--;
	}

	/* Do we have a pathspec? */
	pathspec = (argc > 1) ? argv + 1 : NULL;

	if (find_copies_harder && detect_rename != DIFF_DETECT_COPY)
		usage(diff_files_usage);

	/* At this point, if argc == 1, then we are doing everything.
	 * Otherwise argv[1] .. argv[argc-1] have the explicit paths.
	 */
	if (entries < 0) {
		perror("read_cache");
		exit(1);
	}

	diff_setup(diff_setup_opt);

	for (i = 0; i < entries; i++) {
		struct stat st;
		unsigned int oldmode;
		struct cache_entry *ce = active_cache[i];
		int changed;

		if (!ce_path_match(ce, pathspec))
			continue;

		if (ce_stage(ce)) {
			show_unmerge(ce->name);
			while (i < entries &&
			       !strcmp(ce->name, active_cache[i]->name))
				i++;
			i--; /* compensate for loop control increments */
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
		if (!changed && !find_copies_harder)
			continue;
		oldmode = ntohl(ce->ce_mode);
		show_modified(oldmode, DIFF_FILE_CANON_MODE(st.st_mode),
			      ce->sha1, (changed ? null_sha1 : ce->sha1),
			      ce->name);
	}
	diffcore_std(pathspec, 
		     detect_rename, diff_score_opt,
		     pickaxe, pickaxe_opts,
		     diff_break_opt,
		     orderfile, diff_filter);
	diff_flush(diff_output_format, diff_line_termination);
	return 0;
}
