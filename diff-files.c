/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "diff.h"

static const char *diff_files_usage =
"diff-files [-p] [-q] [-r] [-z] [paths...]";

static int generate_patch = 0;
static int line_termination = '\n';
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
	if (generate_patch)
		diff_unmerge(path);
	else
		printf("U %s%c", path, line_termination);
}

static void show_file(int pfx, struct cache_entry *ce)
{
	if (generate_patch)
		diff_addremove(pfx, ntohl(ce->ce_mode), ce->sha1,
			       ce->name, NULL);
	else
		printf("%c%06o\t%s\t%s\t%s%c",
		       pfx, ntohl(ce->ce_mode), "blob",
		       sha1_to_hex(ce->sha1), ce->name, line_termination);
}

static void show_modified(int oldmode, int mode,
			  const char *old_sha1, const char *sha1,
			  char *path)
{
	char old_sha1_hex[41];
	strcpy(old_sha1_hex, sha1_to_hex(old_sha1));

	if (generate_patch)
		diff_change(oldmode, mode, old_sha1, sha1, path, NULL);
	else
		printf("*%06o->%06o\tblob\t%s->%s\t%s%c",
		       oldmode, mode, old_sha1_hex, sha1_to_hex(sha1), path,
		       line_termination);
}

int main(int argc, char **argv)
{
	static const char null_sha1[20] = { 0, };
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
			if (errno != ENOENT) {
				perror(ce->name);
				continue;
			}	
			if (silent)
				continue;
			show_file('-', ce);
			continue;
		}
		changed = cache_match_stat(ce, &st);
		if (!changed)
			continue;

		oldmode = ntohl(ce->ce_mode);
		mode = S_IFREG | ce_permissions(st.st_mode);

		show_modified(oldmode, mode, ce->sha1, null_sha1,
			      ce->name);
	}
	return 0;
}
