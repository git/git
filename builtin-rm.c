/*
 * "git rm" builtin command
 *
 * Copyright (C) Linus Torvalds 2006
 */
#include "cache.h"
#include "builtin.h"
#include "dir.h"
#include "cache-tree.h"

static const char builtin_rm_usage[] =
"git-rm [-n] [-v] [-f] <filepattern>...";

static struct {
	int nr, alloc;
	const char **name;
} list;

static void add_list(const char *name)
{
	if (list.nr >= list.alloc) {
		list.alloc = alloc_nr(list.alloc);
		list.name = xrealloc(list.name, list.alloc * sizeof(const char *));
	}
	list.name[list.nr++] = name;
}

static int remove_file(const char *name)
{
	int ret;
	char *slash;

	ret = unlink(name);
	if (!ret && (slash = strrchr(name, '/'))) {
		char *n = strdup(name);
		do {
			n[slash - name] = 0;
			name = n;
		} while (!rmdir(name) && (slash = strrchr(name, '/')));
	}
	return ret;
}

static struct lock_file lock_file;

int cmd_rm(int argc, const char **argv, char **envp)
{
	int i, newfd;
	int verbose = 0, show_only = 0, force = 0;
	const char *prefix = setup_git_directory();
	const char **pathspec;
	char *seen;

	git_config(git_default_config);

	newfd = hold_lock_file_for_update(&lock_file, get_index_file());
	if (newfd < 0)
		die("unable to create new index file");

	if (read_cache() < 0)
		die("index file corrupt");

	for (i = 1 ; i < argc ; i++) {
		const char *arg = argv[i];

		if (*arg != '-')
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
		if (!strcmp(arg, "-f")) {
			force = 1;
			continue;
		}
		die(builtin_rm_usage);
	}
	if (argc <= i)
		usage(builtin_rm_usage);

	pathspec = get_pathspec(prefix, argv + i);
	seen = NULL;
	for (i = 0; pathspec[i] ; i++)
		/* nothing */;
	seen = xmalloc(i);
	memset(seen, 0, i);

	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (!match_pathspec(pathspec, ce->name, ce_namelen(ce), 0, seen))
			continue;
		add_list(ce->name);
	}

	if (pathspec) {
		const char *match;
		for (i = 0; (match = pathspec[i]) != NULL ; i++) {
			if (*match && !seen[i])
				die("pathspec '%s' did not match any files", match);
		}
	}

	/*
	 * First remove the names from the index: we won't commit
	 * the index unless all of them succeed
	 */
	for (i = 0; i < list.nr; i++) {
		const char *path = list.name[i];
		printf("rm '%s'\n", path);

		if (remove_file_from_cache(path))
			die("git rm: unable to remove %s", path);
		cache_tree_invalidate_path(active_cache_tree, path);
	}

	if (show_only)
		return 0;

	/*
	 * Then, if we used "-f", remove the filenames from the
	 * workspace. If we fail to remove the first one, we
	 * abort the "git rm" (but once we've successfully removed
	 * any file at all, we'll go ahead and commit to it all:
	 * by then we've already committed ourself and can't fail
	 * in the middle)
	 */
	if (force) {
		int removed = 0;
		for (i = 0; i < list.nr; i++) {
			const char *path = list.name[i];
			if (!remove_file(path)) {
				removed = 1;
				continue;
			}
			if (!removed)
				die("git rm: %s: %s", path, strerror(errno));
		}
	}

	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_lock_file(&lock_file))
			die("Unable to write new index file");
	}

	return 0;
}
