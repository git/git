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
#include "parse-options.h"
#include "submodule.h"

static const char * const builtin_rm_usage[] = {
	N_("git rm [options] [--] <file>..."),
	NULL
};

static struct {
	int nr, alloc;
	struct {
		const char *name;
		char is_submodule;
	} *entry;
} list;

static int get_ours_cache_pos(const char *path, int pos)
{
	int i = -pos - 1;

	while ((i < active_nr) && !strcmp(active_cache[i]->name, path)) {
		if (ce_stage(active_cache[i]) == 2)
			return i;
		i++;
	}
	return -1;
}

static int check_submodules_use_gitfiles(void)
{
	int i;
	int errs = 0;

	for (i = 0; i < list.nr; i++) {
		const char *name = list.entry[i].name;
		int pos;
		struct cache_entry *ce;
		struct stat st;

		pos = cache_name_pos(name, strlen(name));
		if (pos < 0) {
			pos = get_ours_cache_pos(name, pos);
			if (pos < 0)
				continue;
		}
		ce = active_cache[pos];

		if (!S_ISGITLINK(ce->ce_mode) ||
		    (lstat(ce->name, &st) < 0) ||
		    is_empty_dir(name))
			continue;

		if (!submodule_uses_gitfile(name))
			errs = error(_("submodule '%s' (or one of its nested "
				     "submodules) uses a .git directory\n"
				     "(use 'rm -rf' if you really want to remove "
				     "it including all of its history)"), name);
	}

	return errs;
}

static int check_local_mod(unsigned char *head, int index_only)
{
	/*
	 * Items in list are already sorted in the cache order,
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
		const char *name = list.entry[i].name;
		unsigned char sha1[20];
		unsigned mode;
		int local_changes = 0;
		int staged_changes = 0;

		pos = cache_name_pos(name, strlen(name));
		if (pos < 0) {
			/*
			 * Skip unmerged entries except for populated submodules
			 * that could lose history when removed.
			 */
			pos = get_ours_cache_pos(name, pos);
			if (pos < 0)
				continue;

			if (!S_ISGITLINK(active_cache[pos]->ce_mode) ||
			    is_empty_dir(name))
				continue;
		}
		ce = active_cache[pos];

		if (lstat(ce->name, &st) < 0) {
			if (errno != ENOENT)
				warning("'%s': %s", ce->name, strerror(errno));
			/* It already vanished from the working tree */
			continue;
		}
		else if (S_ISDIR(st.st_mode)) {
			/* if a file was removed and it is now a
			 * directory, that is the same as ENOENT as
			 * far as git is concerned; we do not track
			 * directories unless they are submodules.
			 */
			if (!S_ISGITLINK(ce->ce_mode))
				continue;
		}

		/*
		 * "rm" of a path that has changes need to be treated
		 * carefully not to allow losing local changes
		 * accidentally.  A local change could be (1) file in
		 * work tree is different since the index; and/or (2)
		 * the user staged a content that is different from
		 * the current commit in the index.
		 *
		 * In such a case, you would need to --force the
		 * removal.  However, "rm --cached" (remove only from
		 * the index) is safe if the index matches the file in
		 * the work tree or the HEAD commit, as it means that
		 * the content being removed is available elsewhere.
		 */

		/*
		 * Is the index different from the file in the work tree?
		 * If it's a submodule, is its work tree modified?
		 */
		if (ce_match_stat(ce, &st, 0) ||
		    (S_ISGITLINK(ce->ce_mode) &&
		     !ok_to_remove_submodule(ce->name)))
			local_changes = 1;

		/*
		 * Is the index different from the HEAD commit?  By
		 * definition, before the very initial commit,
		 * anything staged in the index is treated by the same
		 * way as changed from the HEAD.
		 */
		if (no_head
		     || get_tree_entry(head, name, sha1, &mode)
		     || ce->ce_mode != create_ce_mode(mode)
		     || hashcmp(ce->sha1, sha1))
			staged_changes = 1;

		/*
		 * If the index does not match the file in the work
		 * tree and if it does not match the HEAD commit
		 * either, (1) "git rm" without --cached definitely
		 * will lose information; (2) "git rm --cached" will
		 * lose information unless it is about removing an
		 * "intent to add" entry.
		 */
		if (local_changes && staged_changes) {
			if (!index_only || !(ce->ce_flags & CE_INTENT_TO_ADD))
				errs = error(_("'%s' has staged content different "
					     "from both the file and the HEAD\n"
					     "(use -f to force removal)"), name);
		}
		else if (!index_only) {
			if (staged_changes)
				errs = error(_("'%s' has changes staged in the index\n"
					     "(use --cached to keep the file, "
					     "or -f to force removal)"), name);
			if (local_changes) {
				if (S_ISGITLINK(ce->ce_mode) &&
				    !submodule_uses_gitfile(name)) {
					errs = error(_("submodule '%s' (or one of its nested "
						     "submodules) uses a .git directory\n"
						     "(use 'rm -rf' if you really want to remove "
						     "it including all of its history)"), name);
				} else
					errs = error(_("'%s' has local modifications\n"
						     "(use --cached to keep the file, "
						     "or -f to force removal)"), name);
			}
		}
	}
	return errs;
}

static struct lock_file lock_file;

static int show_only = 0, force = 0, index_only = 0, recursive = 0, quiet = 0;
static int ignore_unmatch = 0;

static struct option builtin_rm_options[] = {
	OPT__DRY_RUN(&show_only, N_("dry run")),
	OPT__QUIET(&quiet, N_("do not list removed files")),
	OPT_BOOLEAN( 0 , "cached",         &index_only, N_("only remove from the index")),
	OPT__FORCE(&force, N_("override the up-to-date check")),
	OPT_BOOLEAN('r', NULL,             &recursive,  N_("allow recursive removal")),
	OPT_BOOLEAN( 0 , "ignore-unmatch", &ignore_unmatch,
				N_("exit with a zero status even if nothing matched")),
	OPT_END(),
};

int cmd_rm(int argc, const char **argv, const char *prefix)
{
	int i, newfd;
	const char **pathspec;
	char *seen;

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_rm_options,
			     builtin_rm_usage, 0);
	if (!argc)
		usage_with_options(builtin_rm_usage, builtin_rm_options);

	if (!index_only)
		setup_work_tree();

	newfd = hold_locked_index(&lock_file, 1);

	if (read_cache() < 0)
		die(_("index file corrupt"));

	/*
	 * Drop trailing directory separators from directories so we'll find
	 * submodules in the index.
	 */
	for (i = 0; i < argc; i++) {
		size_t pathlen = strlen(argv[i]);
		if (pathlen && is_dir_sep(argv[i][pathlen - 1]) &&
		    is_directory(argv[i])) {
			do {
				pathlen--;
			} while (pathlen && is_dir_sep(argv[i][pathlen - 1]));
			argv[i] = xmemdupz(argv[i], pathlen);
		}
	}

	pathspec = get_pathspec(prefix, argv);
	refresh_index(&the_index, REFRESH_QUIET, pathspec, NULL, NULL);

	seen = NULL;
	for (i = 0; pathspec[i] ; i++)
		/* nothing */;
	seen = xcalloc(i, 1);

	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (!match_pathspec(pathspec, ce->name, ce_namelen(ce), 0, seen))
			continue;
		ALLOC_GROW(list.entry, list.nr + 1, list.alloc);
		list.entry[list.nr].name = ce->name;
		list.entry[list.nr++].is_submodule = S_ISGITLINK(ce->ce_mode);
	}

	if (pathspec) {
		const char *match;
		int seen_any = 0;
		for (i = 0; (match = pathspec[i]) != NULL ; i++) {
			if (!seen[i]) {
				if (!ignore_unmatch) {
					die(_("pathspec '%s' did not match any files"),
					    match);
				}
			}
			else {
				seen_any = 1;
			}
			if (!recursive && seen[i] == MATCHED_RECURSIVELY)
				die(_("not removing '%s' recursively without -r"),
				    *match ? match : ".");
		}

		if (! seen_any)
			exit(0);
	}

	/*
	 * If not forced, the file, the index and the HEAD (if exists)
	 * must match; but the file can already been removed, since
	 * this sequence is a natural "novice" way:
	 *
	 *	rm F; git rm F
	 *
	 * Further, if HEAD commit exists, "diff-index --cached" must
	 * report no changes unless forced.
	 */
	if (!force) {
		unsigned char sha1[20];
		if (get_sha1("HEAD", sha1))
			hashclr(sha1);
		if (check_local_mod(sha1, index_only))
			exit(1);
	} else if (!index_only) {
		if (check_submodules_use_gitfiles())
			exit(1);
	}

	/*
	 * First remove the names from the index: we won't commit
	 * the index unless all of them succeed.
	 */
	for (i = 0; i < list.nr; i++) {
		const char *path = list.entry[i].name;
		if (!quiet)
			printf("rm '%s'\n", path);

		if (remove_file_from_cache(path))
			die(_("git rm: unable to remove %s"), path);
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
			const char *path = list.entry[i].name;
			if (list.entry[i].is_submodule) {
				if (is_empty_dir(path)) {
					if (!rmdir(path)) {
						removed = 1;
						continue;
					}
				} else {
					struct strbuf buf = STRBUF_INIT;
					strbuf_addstr(&buf, path);
					if (!remove_dir_recursively(&buf, 0)) {
						removed = 1;
						strbuf_release(&buf);
						continue;
					}
					strbuf_release(&buf);
					/* Fallthrough and let remove_path() fail. */
				}
			}
			if (!remove_path(path)) {
				removed = 1;
				continue;
			}
			if (!removed)
				die_errno("git rm: '%s'", path);
		}
	}

	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_locked_index(&lock_file))
			die(_("Unable to write new index file"));
	}

	return 0;
}
