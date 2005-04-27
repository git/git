/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static const char *show_diff_usage = "show-diff [-q] [-s] [-z] [paths...]";

static int recursive = 0;
static int line_termination = '\n';
static int silent = 0;
static int silent_on_nonexisting_files = 0;

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

static void show_file(const char *prefix, struct cache_entry *ce)
{
	printf("%s%o\t%s\t%s\t%s%c", prefix, ntohl(ce->ce_mode), "blob",
		sha1_to_hex(ce->sha1), ce->name, line_termination);
}

int main(int argc, char **argv)
{
	static const char null_sha1_hex[] = "0000000000000000000000000000000000000000";
	int entries = read_cache();
	int i;

	while (1 < argc && argv[1][0] == '-') {
		if (!strcmp(argv[1], "-s"))
			silent_on_nonexisting_files = silent = 1;
		else if (!strcmp(argv[1], "-q"))
			silent_on_nonexisting_files = 1;
		else if (!strcmp(argv[1], "-z"))
			line_termination = 0;
		else if (!strcmp(argv[1], "-r"))
			recursive = 1;		/* No-op */
		else
			usage(show_diff_usage);
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
			show_file("U", ce);

			while (i < entries &&
			       !strcmp(ce->name, active_cache[i]->name))
				i++;
			i--; /* compensate for loop control increments */
			continue;
		}
 
		if (stat(ce->name, &st) < 0) {
			if (errno != ENOENT) {
				perror(ce->name);
				continue;
			}	
			if (silent_on_nonexisting_files)
				continue;
			show_file("-", ce);
			continue;
		}
		changed = cache_match_stat(ce, &st);
		if (!changed)
			continue;

		oldmode = ntohl(ce->ce_mode);
		mode = S_IFREG | ce_permissions(st.st_mode);

		printf("*%o->%o\t%s\t%s->%s\t%s%c",
			oldmode, mode, "blob",
			sha1_to_hex(ce->sha1), null_sha1_hex,
			ce->name, line_termination);
	}
	return 0;
}
