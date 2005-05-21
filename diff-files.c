/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "diff.h"

static const char *diff_files_usage =
"git-diff-files [-p] [-q] [-r] [-z] [-M] [-C] [-R] [paths...]";

static int generate_patch = 0;
static int line_termination = '\n';
static int detect_rename = 0;
static int reverse_diff = 0;
static int diff_score_opt = 0;
static int silent = 0;

static int matches_pathspec(struct cache_entry *ce, char **spec, int cnt)
{
	int i;
	int namelen = ce_namelen(ce);
	for (i = 0; i < cnt; i++) {
		int speclen = strlen(spec[i]);
		if (! strncmp(spec[i], ce->name, speclen) &&
		    speclen <= namelen &&
		    (ce->name[speclen] == 0 ||
		     ce->name[speclen] == '/'))
			return 1;
	}
	return 0;
}

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

int main(int argc, char **argv)
{
	static const unsigned char null_sha1[20] = { 0, };
	int entries = read_cache();
	int i;

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-p"))
			generate_patch = 1;
		else if (!strcmp(argv[1], "-q"))
			silent = 1;
		else if (!strcmp(argv[1], "-r"))
			; /* no-op */
		else if (!strcmp(argv[1], "-s"))
			; /* no-op */
		else if (!strcmp(argv[1], "-z"))
			line_termination = 0;
		else if (!strcmp(argv[1], "-R"))
			reverse_diff = 1;
		else if (!strncmp(argv[1], "-M", 2)) {
			diff_score_opt = diff_scoreopt_parse(argv[1]);
			detect_rename = generate_patch = 1;
		}
		else if (!strncmp(argv[1], "-C", 2)) {
			diff_score_opt = diff_scoreopt_parse(argv[1]);
			detect_rename = 2;
			generate_patch = 1;
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

	diff_setup(detect_rename, diff_score_opt, reverse_diff,
		   (generate_patch ? -1 : line_termination),
		   NULL, 0);

	for (i = 0; i < entries; i++) {
		struct stat st;
		unsigned int oldmode, mode;
		struct cache_entry *ce = active_cache[i];
		int changed;

		if (1 < argc &&
		    ! matches_pathspec(ce, argv+1, argc-1))
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
		if (!changed)
			continue;

		oldmode = ntohl(ce->ce_mode);
		mode = (S_ISLNK(st.st_mode) ? S_IFLNK :
			S_IFREG | ce_permissions(st.st_mode));

		show_modified(oldmode, mode, ce->sha1, null_sha1,
			      ce->name);
	}
	diff_flush();
	return 0;
}
