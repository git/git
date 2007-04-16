/*
 * "git rm" builtin command
 *
 * Copyright (C) Linus Torvalds 2006
 */
#include "cache.h"
#include "builtin.h"
#include "dir.h"
#include "cache-tree.h"
#include "tree-walk.h"

static const char builtin_rm_usage[] =
"git-rm [-f] [-n] [-r] [--cached] [--quiet] [--] <file>...";

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
	if (ret && errno == ENOENT)
		/* The user has removed it from the filesystem by hand */
		ret = errno = 0;

	if (!ret && (slash = strrchr(name, '/'))) {
		char *n = xstrdup(name);
		do {
			n[slash - name] = 0;
			name = n;
		} while (!rmdir(name) && (slash = strrchr(name, '/')));
	}
	return ret;
}

static int check_local_mod(unsigned char *head)
{
	/* items in list are already sorted in the cache order,
	 * so we could do this a lot more efficiently by using
	 * tree_desc based traversal if we wanted to, but I am
	 * lazy, and who cares if removal of files is a tad
	 * slower than the theoretical maximum speed?
	 */
	int i, no_head;
	int errs = 0;

	no_head = is_null_sha1(head);
	for (i = 0; i < list.nr; i++) {
		struct stat st;
		int pos;
		struct cache_entry *ce;
		const char *name = list.name[i];
		unsigned char sha1[20];
		unsigned mode;

		pos = cache_name_pos(name, strlen(name));
		if (pos < 0)
			continue; /* removing unmerged entry */
		ce = active_cache[pos];

		if (lstat(ce->name, &st) < 0) {
			if (errno != ENOENT)
				fprintf(stderr, "warning: '%s': %s",
					ce->name, strerror(errno));
			/* It already vanished from the working tree */
			continue;
		}
		else if (S_ISDIR(st.st_mode)) {
			/* if a file was removed and it is now a
			 * directory, that is the same as ENOENT as
			 * far as git is concerned; we do not track
			 * directories.
			 */
			continue;
		}
		if (ce_match_stat(ce, &st, 0))
			errs = error("'%s' has local modifications "
				     "(hint: try -f)", ce->name);
		if (no_head
		     || get_tree_entry(head, name, sha1, &mode)
		     || ce->ce_mode != create_ce_mode(mode)
		     || hashcmp(ce->sha1, sha1))
			errs = error("'%s' has changes staged in the index "
				     "(hint: try -f)", name);
	}
	return errs;
}

static struct lock_file lock_file;

int cmd_rm(int argc, const char **argv, const char *prefix)
{
	int i, newfd;
	int show_only = 0, force = 0, index_only = 0, recursive = 0, quiet = 0;
	const char **pathspec;
	char *seen;

	git_config(git_default_config);

	newfd = hold_locked_index(&lock_file, 1);

	if (read_cache() < 0)
		die("index file corrupt");

	for (i = 1 ; i < argc ; i++) {
		const char *arg = argv[i];

		if (*arg != '-')
			break;
		else if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		else if (!strcmp(arg, "-n"))
			show_only = 1;
		else if (!strcmp(arg, "--cached"))
			index_only = 1;
		else if (!strcmp(arg, "-f"))
			force = 1;
		else if (!strcmp(arg, "-r"))
			recursive = 1;
		else if (!strcmp(arg, "--quiet"))
			quiet = 1;
		else
			usage(builtin_rm_usage);
	}
	if (argc <= i)
		usage(builtin_rm_usage);

	pathspec = get_pathspec(prefix, argv + i);
	seen = NULL;
	for (i = 0; pathspec[i] ; i++)
		/* nothing */;
	seen = xcalloc(i, 1);

	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (!match_pathspec(pathspec, ce->name, ce_namelen(ce), 0, seen))
			continue;
		add_list(ce->name);
	}

	if (pathspec) {
		const char *match;
		for (i = 0; (match = pathspec[i]) != NULL ; i++) {
			if (!seen[i])
				die("pathspec '%s' did not match any files",
				    match);
			if (!recursive && seen[i] == MATCHED_RECURSIVELY)
				die("not removing '%s' recursively without -r",
				    *match ? match : ".");
		}
	}

	/*
	 * If not forced, the file, the index and the HEAD (if exists)
	 * must match; but the file can already been removed, since
	 * this sequence is a natural "novice" way:
	 *
	 *	rm F; git fm F
	 *
	 * Further, if HEAD commit exists, "diff-index --cached" must
	 * report no changes unless forced.
	 */
	if (!force) {
		unsigned char sha1[20];
		if (get_sha1("HEAD", sha1))
			hashclr(sha1);
		if (check_local_mod(sha1))
			exit(1);
	}

	/*
	 * First remove the names from the index: we won't commit
	 * the index unless all of them succeed.
	 */
	for (i = 0; i < list.nr; i++) {
		const char *path = list.name[i];
		if (!quiet)
			printf("rm '%s'\n", path);

		if (remove_file_from_cache(path))
			die("git-rm: unable to remove %s", path);
		cache_tree_invalidate_path(active_cache_tree, path);
	}

	if (show_only)
		return 0;

	/*
	 * Then, unless we used "--cached", remove the filenames from
	 * the workspace. If we fail to remove the first one, we
	 * abort the "git rm" (but once we've successfully removed
	 * any file at all, we'll go ahead and commit to it all:
	 * by then we've already committed ourselves and can't fail
	 * in the middle)
	 */
	if (!index_only) {
		int removed = 0;
		for (i = 0; i < list.nr; i++) {
			const char *path = list.name[i];
			if (!remove_file(path)) {
				removed = 1;
				continue;
			}
			if (!removed)
				die("git-rm: %s: %s", path, strerror(errno));
		}
	}

	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    close(newfd) || commit_locked_index(&lock_file))
			die("Unable to write new index file");
	}

	return 0;
}
