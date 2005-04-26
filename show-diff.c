/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "diff.h"

static const char *show_diff_usage = "show-diff [-q] [-s] [-z] [paths...]";

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

int main(int argc, char **argv)
{
	int silent = 0;
	int silent_on_nonexisting_files = 0;
	int machine_readable = 0;
	int reverse = 0;
	int entries = read_cache();
	int i;

	while (1 < argc && argv[1][0] == '-') {
		if  (!strcmp(argv[1], "-R"))
			reverse = 1;
		else if (!strcmp(argv[1], "-s"))
			silent_on_nonexisting_files = silent = 1;
		else if (!strcmp(argv[1], "-q"))
			silent_on_nonexisting_files = 1;
		else if (!strcmp(argv[1], "-z"))
			machine_readable = 1;
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
		struct cache_entry *ce = active_cache[i];
		int changed;

		if (1 < argc &&
		    ! matches_pathspec(ce, argv+1, argc-1))
			continue;

		if (ce_stage(ce)) {
			if (machine_readable)
				printf("U %s%c", ce->name, 0);
			else
				printf("%s: Unmerged\n",
				       ce->name);
			while (i < entries &&
			       !strcmp(ce->name, active_cache[i]->name))
				i++;
			i--; /* compensate for loop control increments */
			continue;
		}
 
		if (stat(ce->name, &st) < 0) {
			if (errno == ENOENT && silent_on_nonexisting_files)
				continue;
			if (machine_readable)
				printf("X %s%c", ce->name, 0);
			else {
				printf("%s: %s\n", ce->name, strerror(errno));
				if (errno == ENOENT)
					show_diff_empty(ce, reverse);
			}
			continue;
		}
		changed = cache_match_stat(ce, &st);
		if (!changed)
			continue;
		if (!machine_readable)
			printf("%s: %s\n", ce->name, sha1_to_hex(ce->sha1));
		else {
			printf("%s %s%c", sha1_to_hex(ce->sha1), ce->name, 0);
			continue;
		}
		if (silent)
			continue;

		show_differences(ce, reverse);
	}
	return 0;
}
