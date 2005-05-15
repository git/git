/*
 * check-files.c
 *
 * Check that a set of files are up-to-date in the filesystem or
 * do not exist. Used to verify a patch target before doing a patch.
 *
 * Copyright (C) 2005 Linus Torvalds
 */
#include "cache.h"

static void check_file(const char *path)
{
	int fd = open(path, O_RDONLY);
	struct cache_entry *ce;
	struct stat st;
	int pos, changed;

	/* Nonexistent is fine */
	if (fd < 0) {
		if (errno != ENOENT)
			die("%s: %s", path, strerror(errno));
		return;
	}

	/* Exists but is not in the cache is not fine */
	pos = cache_name_pos(path, strlen(path));
	if (pos < 0)
		die("preparing to update existing file '%s' not in cache", path);
	ce = active_cache[pos];

	if (lstat(path, &st) < 0)
		die("lstat(%s): %s", path, strerror(errno));

	changed = ce_match_stat(ce, &st);
	if (changed)
		die("preparing to update file '%s' not uptodate in cache", path);
}

int main(int argc, char **argv)
{
	int i;

	read_cache();
	for (i = 1; i < argc ; i++)
		check_file(argv[i]);
	return 0;
}
