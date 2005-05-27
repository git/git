/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "diff.h"

static const char *diff_files_usage =
"git-diff-files [-p] [-q] [-r] [-z] [-M] [-C] [-R] [-S<string>] [paths...]";

static int diff_output_format = DIFF_FORMAT_HUMAN;
static int detect_rename = 0;
static int diff_setup_opt = 0;
static int diff_score_opt = 0;
static const char *pickaxe = NULL;
static int pickaxe_opts = 0;
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
	int entries = read_cache();
	int i;

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-p"))
			diff_output_format = DIFF_FORMAT_PATCH;
		else if (!strcmp(argv[1], "-q"))
			silent = 1;
		else if (!strcmp(argv[1], "-r"))
			; /* no-op */
		else if (!strcmp(argv[1], "-s"))
			; /* no-op */
		else if (!strcmp(argv[1], "-z"))
			diff_output_format = DIFF_FORMAT_MACHINE;
		else if (!strcmp(argv[1], "-R"))
			diff_setup_opt |= DIFF_SETUP_REVERSE;
		else if (!strcmp(argv[1], "-S"))
			pickaxe = argv[1] + 2;
		else if (!strcmp(argv[1], "--pickaxe-all"))
			pickaxe_opts = DIFF_PICKAXE_ALL;
		else if (!strncmp(argv[1], "-M", 2)) {
			diff_score_opt = diff_scoreopt_parse(argv[1]);
			detect_rename = DIFF_DETECT_RENAME;
		}
		else if (!strncmp(argv[1], "-C", 2)) {
			diff_score_opt = diff_scoreopt_parse(argv[1]);
			detect_rename = DIFF_DETECT_COPY;
		}
		else
			usage(diff_files_usage);
		argv++; argc--;
	}

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
		unsigned int oldmode, mode;
		struct cache_entry *ce = active_cache[i];
		int changed;

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
		if (!changed && detect_rename < DIFF_DETECT_COPY)
			continue;

		oldmode = ntohl(ce->ce_mode);
		mode = (S_ISLNK(st.st_mode) ? S_IFLNK :
			S_IFREG | ce_permissions(st.st_mode));

		show_modified(oldmode, mode, ce->sha1, null_sha1,
			      ce->name);
	}
	if (1 < argc)
		diffcore_pathspec(argv + 1);
	if (detect_rename)
		diffcore_rename(detect_rename, diff_score_opt);
	if (pickaxe)
		diffcore_pickaxe(pickaxe, pickaxe_opts);
	diff_flush(diff_output_format, 1);
	return 0;
}
