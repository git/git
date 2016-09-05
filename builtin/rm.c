/*
 * "git rm" builtin command
 *
 * Copyright (C) Linus Torvalds 2006
 */
#include "builtin.h"
#include "lockfile.h"
#include "dir.h"
#include "cache-tree.h"
#include "tree-walk.h"
#include "parse-options.h"
#include "string-list.h"
#include "submodule.h"
#include "pathspec.h"

static const char * const builtin_rm_usage[] = {
	N_("git rm [<options>] [--] <file>..."),
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

static void print_error_files(struct string_list *files_list,
			      const char *main_msg,
			      const char *hints_msg,
			      int *errs)
{
	if (files_list->nr) {
		int i;
		struct strbuf err_msg = STRBUF_INIT;

		strbuf_addstr(&err_msg, main_msg);
		for (i = 0; i < files_list->nr; i++)
			strbuf_addf(&err_msg,
				    "\n    %s",
				    files_list->items[i].string);
		if (advice_rm_hints)
			strbuf_addstr(&err_msg, hints_msg);
		*errs = error("%s", err_msg.buf);
		strbuf_release(&err_msg);
	}
}

static void error_removing_concrete_submodules(struct string_list *files, int *errs)
{
	print_error_files(files,
			  Q_("the following submodule (or one of its nested "
			     "submodules)\n"
			     "uses a .git directory:",
			     "the following submodules (or one of their nested "
			     "submodules)\n"
			     "use a .git directory:", files->nr),
			  _("\n(use 'rm -rf' if you really want to remove "
			    "it including all of its history)"),
			  errs);
	string_list_clear(files, 0);
}

static int check_submodules_use_gitfiles(void)
{
	int i;
	int errs = 0;
	struct string_list files = STRING_LIST_INIT_NODUP;

	for (i = 0; i < list.nr; i++) {
		const char *name = list.entry[i].name;
		int pos;
		const struct cache_entry *ce;

		pos = cache_name_pos(name, strlen(name));
		if (pos < 0) {
			pos = get_ours_cache_pos(name, pos);
			if (pos < 0)
				continue;
		}
		ce = active_cache[pos];

		if (!S_ISGITLINK(ce->ce_mode) ||
		    !file_exists(ce->name) ||
		    is_empty_dir(name))
			continue;

		if (!submodule_uses_gitfile(name))
			string_list_append(&files, name);
	}

	error_removing_concrete_submodules(&files, &errs);

	return errs;
}

static int check_local_mod(struct object_id *head, int index_only)
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
	struct string_list files_staged = STRING_LIST_INIT_NODUP;
	struct string_list files_cached = STRING_LIST_INIT_NODUP;
	struct string_list files_submodule = STRING_LIST_INIT_NODUP;
	struct string_list files_local = STRING_LIST_INIT_NODUP;

	no_head = is_null_oid(head);
	for (i = 0; i < list.nr; i++) {
		struct stat st;
		int pos;
		const struct cache_entry *ce;
		const char *name = list.entry[i].name;
		struct object_id oid;
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
			if (errno != ENOENT && errno != ENOTDIR)
				warning_errno(_("failed to stat '%s'"), ce->name);
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
		     || get_tree_entry(head->hash, name, oid.hash, &mode)
		     || ce->ce_mode != create_ce_mode(mode)
		     || oidcmp(&ce->oid, &oid))
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
			if (!index_only || !ce_intent_to_add(ce))
				string_list_append(&files_staged, name);
		}
		else if (!index_only) {
			if (staged_changes)
				string_list_append(&files_cached, name);
			if (local_changes) {
				if (S_ISGITLINK(ce->ce_mode) &&
				    !submodule_uses_gitfile(name))
					string_list_append(&files_submodule, name);
				else
					string_list_append(&files_local, name);
			}
		}
	}
	print_error_files(&files_staged,
			  Q_("the following file has staged content different "
			     "from both the\nfile and the HEAD:",
			     "the following files have staged content different"
			     " from both the\nfile and the HEAD:",
			     files_staged.nr),
			  _("\n(use -f to force removal)"),
			  &errs);
	string_list_clear(&files_staged, 0);
	print_error_files(&files_cached,
			  Q_("the following file has changes "
			     "staged in the index:",
			     "the following files have changes "
			     "staged in the index:", files_cached.nr),
			  _("\n(use --cached to keep the file,"
			    " or -f to force removal)"),
			  &errs);
	string_list_clear(&files_cached, 0);

	error_removing_concrete_submodules(&files_submodule, &errs);

	print_error_files(&files_local,
			  Q_("the following file has local modifications:",
			     "the following files have local modifications:",
			     files_local.nr),
			  _("\n(use --cached to keep the file,"
			    " or -f to force removal)"),
			  &errs);
	string_list_clear(&files_local, 0);

	return errs;
}

static struct lock_file lock_file;

static int show_only = 0, force = 0, index_only = 0, recursive = 0, quiet = 0;
static int ignore_unmatch = 0;

static struct option builtin_rm_options[] = {
	OPT__DRY_RUN(&show_only, N_("dry run")),
	OPT__QUIET(&quiet, N_("do not list removed files")),
	OPT_BOOL( 0 , "cached",         &index_only, N_("only remove from the index")),
	OPT__FORCE(&force, N_("override the up-to-date check")),
	OPT_BOOL('r', NULL,             &recursive,  N_("allow recursive removal")),
	OPT_BOOL( 0 , "ignore-unmatch", &ignore_unmatch,
				N_("exit with a zero status even if nothing matched")),
	OPT_END(),
};

int cmd_rm(int argc, const char **argv, const char *prefix)
{
	int i;
	struct pathspec pathspec;
	char *seen;

	gitmodules_config();
	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_rm_options,
			     builtin_rm_usage, 0);
	if (!argc)
		usage_with_options(builtin_rm_usage, builtin_rm_options);

	if (!index_only)
		setup_work_tree();

	hold_locked_index(&lock_file, 1);

	if (read_cache() < 0)
		die(_("index file corrupt"));

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_CWD |
		       PATHSPEC_STRIP_SUBMODULE_SLASH_CHEAP,
		       prefix, argv);
	refresh_index(&the_index, REFRESH_QUIET, &pathspec, NULL, NULL);

	seen = xcalloc(pathspec.nr, 1);

	for (i = 0; i < active_nr; i++) {
		const struct cache_entry *ce = active_cache[i];
		if (!ce_path_match(ce, &pathspec, seen))
			continue;
		ALLOC_GROW(list.entry, list.nr + 1, list.alloc);
		list.entry[list.nr].name = xstrdup(ce->name);
		list.entry[list.nr].is_submodule = S_ISGITLINK(ce->ce_mode);
		if (list.entry[list.nr++].is_submodule &&
		    !is_staging_gitmodules_ok())
			die (_("Please stage your changes to .gitmodules or stash them to proceed"));
	}

	if (pathspec.nr) {
		const char *original;
		int seen_any = 0;
		for (i = 0; i < pathspec.nr; i++) {
			original = pathspec.items[i].original;
			if (!seen[i]) {
				if (!ignore_unmatch) {
					die(_("pathspec '%s' did not match any files"),
					    original);
				}
			}
			else {
				seen_any = 1;
			}
			if (!recursive && seen[i] == MATCHED_RECURSIVELY)
				die(_("not removing '%s' recursively without -r"),
				    *original ? original : ".");
		}

		if (!seen_any)
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
		struct object_id oid;
		if (get_oid("HEAD", &oid))
			oidclr(&oid);
		if (check_local_mod(&oid, index_only))
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
		int removed = 0, gitmodules_modified = 0;
		struct strbuf buf = STRBUF_INIT;
		for (i = 0; i < list.nr; i++) {
			const char *path = list.entry[i].name;
			if (list.entry[i].is_submodule) {
				if (is_empty_dir(path)) {
					if (!rmdir(path)) {
						removed = 1;
						if (!remove_path_from_gitmodules(path))
							gitmodules_modified = 1;
						continue;
					}
				} else {
					strbuf_reset(&buf);
					strbuf_addstr(&buf, path);
					if (!remove_dir_recursively(&buf, 0)) {
						removed = 1;
						if (!remove_path_from_gitmodules(path))
							gitmodules_modified = 1;
						strbuf_release(&buf);
						continue;
					} else if (!file_exists(path))
						/* Submodule was removed by user */
						if (!remove_path_from_gitmodules(path))
							gitmodules_modified = 1;
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
		strbuf_release(&buf);
		if (gitmodules_modified)
			stage_updated_gitmodules();
	}

	if (active_cache_changed) {
		if (write_locked_index(&the_index, &lock_file, COMMIT_LOCK))
			die(_("Unable to write new index file"));
	}

	return 0;
}
