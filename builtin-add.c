/*
 * "git add" builtin command
 *
 * Copyright (C) 2006 Linus Torvalds
 */
#include <fnmatch.h>

#include "cache.h"
#include "builtin.h"
#include "dir.h"
#include "cache-tree.h"

static const char builtin_add_usage[] =
"git-add [-n] [-v] <filepattern>...";

static void prune_directory(struct dir_struct *dir, const char **pathspec, int prefix)
{
	char *seen;
	int i, specs;
	struct dir_entry **src, **dst;

	for (specs = 0; pathspec[specs];  specs++)
		/* nothing */;
	seen = xcalloc(specs, 1);

	src = dst = dir->entries;
	i = dir->nr;
	while (--i >= 0) {
		struct dir_entry *entry = *src++;
		if (!match_pathspec(pathspec, entry->name, entry->len, prefix, seen)) {
			free(entry);
			continue;
		}
		*dst++ = entry;
	}
	dir->nr = dst - dir->entries;

	for (i = 0; i < specs; i++) {
		struct stat st;
		const char *match;
		if (seen[i])
			continue;

		/* Existing file? We must have ignored it */
		match = pathspec[i];
		if (!match[0] || !lstat(match, &st))
			continue;
		die("pathspec '%s' did not match any files", match);
	}
}

static void fill_directory(struct dir_struct *dir, const char **pathspec)
{
	const char *path, *base;
	int baselen;

	/* Set up the default git porcelain excludes */
	memset(dir, 0, sizeof(*dir));
	dir->exclude_per_dir = ".gitignore";
	path = git_path("info/exclude");
	if (!access(path, R_OK))
		add_excludes_from_file(dir, path);

	/*
	 * Calculate common prefix for the pathspec, and
	 * use that to optimize the directory walk
	 */
	baselen = common_prefix(pathspec);
	path = ".";
	base = "";
	if (baselen) {
		char *common = xmalloc(baselen + 1);
		common = xmalloc(baselen + 1);
		memcpy(common, *pathspec, baselen);
		common[baselen] = 0;
		path = base = common;
	}

	/* Read the directory and prune it */
	read_directory(dir, path, base, baselen);
	if (pathspec)
		prune_directory(dir, pathspec, baselen);
}

static struct lock_file lock_file;

int cmd_add(int argc, const char **argv, char **envp)
{
	int i, newfd;
	int verbose = 0, show_only = 0;
	const char *prefix = setup_git_directory();
	const char **pathspec;
	struct dir_struct dir;

	git_config(git_default_config);

	newfd = hold_lock_file_for_update(&lock_file, get_index_file());
	if (newfd < 0)
		die("unable to create new index file");

	if (read_cache() < 0)
		die("index file corrupt");

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (arg[0] != '-')
			break;
		if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		if (!strcmp(arg, "-n")) {
			show_only = 1;
			continue;
		}
		if (!strcmp(arg, "-v")) {
			verbose = 1;
			continue;
		}
		die(builtin_add_usage);
	}
	pathspec = get_pathspec(prefix, argv + i);

	fill_directory(&dir, pathspec);

	if (show_only) {
		const char *sep = "", *eof = "";
		for (i = 0; i < dir.nr; i++) {
			printf("%s%s", sep, dir.entries[i]->name);
			sep = " ";
			eof = "\n";
		}
		fputs(eof, stdout);
		return 0;
	}

	for (i = 0; i < dir.nr; i++)
		add_file_to_index(dir.entries[i]->name, verbose);

	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    close(newfd) || commit_lock_file(&lock_file))
			die("Unable to write new index file");
	}

	return 0;
}
