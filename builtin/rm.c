/*
 * "git rm" builtin command
 *
 * Copyright (C) Linus Torvalds 2006
 */
#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "advice.h"
#include "config.h"
#include "lockfile.h"
#include "dir.h"
#include "gettext.h"
#include "hash.h"
#include "tree-walk.h"
#include "object-name.h"
#include "parse-options.h"
#include "read-cache.h"

#include "string-list.h"
#include "setup.h"
#include "sparse-index.h"
#include "submodule.h"
#include "pathspec.h"
#include "../safety-protocol.h"

static const char * const builtin_rm_usage[] = {
	N_("git rm [-f | --force] [-n] [-r] [--cached] [--ignore-unmatch]\n"
	   "       [--quiet] [--pathspec-from-file=<file> [--pathspec-file-nul]]\n"
	   "       [--] [<pathspec>...]"),
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

	while ((i < the_repository->index->cache_nr) && !strcmp(the_repository->index->cache[i]->name, path)) {
		if (ce_stage(the_repository->index->cache[i]) == 2)
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
		if (advice_enabled(ADVICE_RM_HINTS))
			strbuf_addstr(&err_msg, hints_msg);
		*errs = error("%s", err_msg.buf);
		strbuf_release(&err_msg);
	}
}

static void submodules_absorb_gitdir_if_needed(void)
{
	int i;
	for (i = 0; i < list.nr; i++) {
		const char *name = list.entry[i].name;
		int pos;
		const struct cache_entry *ce;

		pos = index_name_pos(the_repository->index, name, strlen(name));
		if (pos < 0) {
			pos = get_ours_cache_pos(name, pos);
			if (pos < 0)
				continue;
		}
		ce = the_repository->index->cache[pos];

		if (!S_ISGITLINK(ce->ce_mode) ||
		    !file_exists(ce->name) ||
		    is_empty_dir(name))
			continue;

		if (!submodule_uses_gitfile(name))
			absorb_git_dir_into_superproject(name, NULL);
	}
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
	struct string_list files_local = STRING_LIST_INIT_NODUP;

	no_head = is_null_oid(head);
	for (i = 0; i < list.nr; i++) {
		struct stat st;
		int pos;
		const struct cache_entry *ce;
		const char *name = list.entry[i].name;
		struct object_id oid;
		unsigned short mode;
		int local_changes = 0;
		int staged_changes = 0;

		pos = index_name_pos(the_repository->index, name, strlen(name));
		if (pos < 0) {
			/*
			 * Skip unmerged entries except for populated submodules
			 * that could lose history when removed.
			 */
			pos = get_ours_cache_pos(name, pos);
			if (pos < 0)
				continue;

			if (!S_ISGITLINK(the_repository->index->cache[pos]->ce_mode) ||
			    is_empty_dir(name))
				continue;
		}
		ce = the_repository->index->cache[pos];

		if (lstat(ce->name, &st) < 0) {
			if (!is_missing_file_error(errno))
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
		if (ie_match_stat(the_repository->index, ce, &st, 0) ||
		    (S_ISGITLINK(ce->ce_mode) &&
		     bad_to_remove_submodule(ce->name,
				SUBMODULE_REMOVAL_DIE_ON_ERROR |
				SUBMODULE_REMOVAL_IGNORE_IGNORED_UNTRACKED)))
			local_changes = 1;

		/*
		 * Is the index different from the HEAD commit?  By
		 * definition, before the very initial commit,
		 * anything staged in the index is treated by the same
		 * way as changed from the HEAD.
		 */
		if (no_head
		     || get_tree_entry(the_repository, head, name, &oid, &mode)
		     || ce->ce_mode != create_ce_mode(mode)
		     || !oideq(&ce->oid, &oid))
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
			if (local_changes)
				string_list_append(&files_local, name);
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

static int show_only = 0, force = 0, index_only = 0, recursive = 0, quiet = 0;
static int ignore_unmatch = 0, pathspec_file_nul;
static int include_sparse;
static char *pathspec_from_file;
static int double_force = 0;

struct rm_safety {
	unsigned int has_nested_git:1;
	unsigned int has_build_artifacts:1;
	unsigned int has_untracked_deps:1;
	unsigned int has_important_files:1;
	unsigned long total_size;
	int file_count;
	struct string_list critical_paths;
};

static struct rm_safety safety = {0};

static const char *critical_patterns[] = {
	/* Build artifacts and dependencies */
	"node_modules/", "vendor/", "build/", "dist/",
	"target/", "bin/", "obj/",
	/* Config and env files */
	".env", "config/", "settings/",
	/* IDE files */
	".idea/", ".vscode/",
	/* Lock files */
	"package-lock.json", "yarn.lock", "Gemfile.lock",
	/* Important project files */
	"README", "LICENSE", "CONTRIBUTING",
	NULL
};

static void check_path_safety(const char *path, struct rm_safety *safety)
{
	struct stat st;
	const char **pattern;

	if (is_nonbare_repository_dir(path))
		safety->has_nested_git = 1;

	/* Check if path matches any critical patterns */
	for (pattern = critical_patterns; *pattern; pattern++) {
		if (strstr(path, *pattern)) {
			safety->has_build_artifacts = 1;
			break;
		}
	}

	/* Gather size information */
	if (lstat(path, &st) == 0) {
		safety->total_size += st.st_size;
		safety->file_count++;
	}
}

static void print_rm_warning(struct rm_safety *safety, int force_level)
{
	struct strbuf msg = STRBUF_INIT;
	int is_dangerous = 0;

	strbuf_addstr(&msg, _("WARNING: You are about to remove files:\n"));

	if (safety->has_nested_git) {
		strbuf_addstr(&msg, _("  ! DANGER: Will remove files from nested Git repositories!\n"));
		is_dangerous = 1;
	}

	if (safety->has_build_artifacts) {
		strbuf_addstr(&msg, _("  ! Will remove build artifacts or dependencies\n"));
		is_dangerous = 1;
	}

	if (safety->has_important_files) {
		strbuf_addstr(&msg, _("  ! Will remove important project files (README, LICENSE, etc)\n"));
		is_dangerous = 1;
	}

	strbuf_addf(&msg, _("  * Total: %d files, %lu bytes will be removed\n"), 
		    safety->file_count, safety->total_size);

	if (is_dangerous && force_level < 2) {
		strbuf_addstr(&msg, _("\nThis operation requires -ff (double force) due to dangerous content\n"));
		die("%s", msg.buf);
	}

	fprintf(stderr, "%s\n", msg.buf);
	strbuf_release(&msg);

	if (is_dangerous) {
		if (!isatty(0)) {
			die(_("Refusing to remove dangerous content in non-interactive mode.\nUse -ff to override or run in terminal"));
		}
		if (!ask(_("Are you ABSOLUTELY sure you want to proceed? Type 'yes' to confirm: "), 0)) {
			die(_("Operation aborted by user"));
		}
	}
}

static struct safety_state rm_safety_state;

static struct option builtin_rm_options[] = {
	OPT__DRY_RUN(&show_only, N_("dry run")),
	OPT__QUIET(&quiet, N_("do not list removed files")),
	OPT_BOOL('f', "force", &force, N_("override the up-to-date check")),
	OPT_BOOL('F', "force-force", &double_force, N_("override all safety checks")),
	OPT_BOOL('r', NULL, &recursive, N_("allow recursive removal")),
	OPT_BOOL( 0 , "cached", &index_only, N_("only remove from the index")),
	OPT_BOOL( 0 , "ignore-unmatch", &ignore_unmatch, N_("exit with a zero status even if nothing matched")),
	OPT_PATHSPEC_FROM_FILE(&pathspec_from_file),
	OPT_PATHSPEC_FILE_NUL(&pathspec_file_nul),
	OPT_END(),
};

int cmd_rm(int argc, const char **argv, const char *prefix, struct repository *repo UNUSED)
{
	int i, newfd;
	struct pathspec pathspec;
	char *seen;
	int force_level = 0;
	int has_safety_concerns = 0;
	int ret = 0;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_rm_usage, builtin_rm_options);

	git_config(git_rm_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_rm_options,
			    builtin_rm_usage, 0);

	if (pathspec_from_file) {
		if (argc) {
			error(_("'%s' and <pathspec>... are mutually exclusive"), "--pathspec-from-file");
			usage_with_options(builtin_rm_usage, builtin_rm_options);
		}

		parse_pathspec_file(&pathspec, 0,
				   PATHSPEC_PREFER_FULL,
				   prefix, pathspec_from_file, pathspec_file_nul);
	} else {
		parse_pathspec(&pathspec, 0,
			      PATHSPEC_PREFER_FULL,
			      prefix, argv);
	}

	if (!pathspec.nr && !ignore_unmatch)
		usage_with_options(builtin_rm_usage, builtin_rm_options);

	if (!index_only)
		setup_work_tree();

	if (!index_only && !force) {
		struct dir_struct dir = DIR_INIT;
		if (repo_read_index(the_repository) < 0)
			die(_("index file corrupt"));
		dir.flags |= DIR_SHOW_IGNORED;
		if (file_exists(".git/info/exclude"))
			dir.exclude_per_dir = ".gitignore";
		fill_directory(&dir, &pathspec);
		refresh_index(&the_index, REFRESH_QUIET|REFRESH_UNMERGED, &pathspec, NULL, NULL);
		for (i = 0; i < dir.nr; i++) {
			struct dir_entry *ent = dir.entries[i];
			check_path_safety(ent->name, &safety);
		}
		print_rm_warning(&safety, force_level);
	}

	prepare_repo_settings(the_repository);
	the_repository->settings.command_requires_full_index = 0;
	repo_hold_locked_index(the_repository, &lock_file, LOCK_DIE_ON_ERROR);

	if (repo_read_index(the_repository) < 0)
		die(_("index file corrupt"));

	refresh_index(the_repository->index, REFRESH_QUIET|REFRESH_UNMERGED, &pathspec, NULL, NULL);

	seen = xcalloc(pathspec.nr, 1);

	if (pathspec_needs_expanded_index(the_repository->index, &pathspec))
		ensure_full_index(the_repository->index);

	/* Initialize safety state */
	safety_init(&rm_safety_state, SAFETY_OP_RM);

	/* Set force level based on flags */
	if (double_force)
		rm_safety_state.force_level = SAFETY_FORCE_DOUBLE;
	else if (force)
		rm_safety_state.force_level = SAFETY_FORCE_SINGLE;

	for (i = 0; i < the_repository->index->cache_nr; i++) {
		const struct cache_entry *ce = the_repository->index->cache[i];

		if (!include_sparse &&
		    (ce_skip_worktree(ce) ||
		     !path_in_sparse_checkout(ce->name, the_repository->index)))
			continue;
		if (!ce_path_match(the_repository->index, ce, &pathspec, seen))
			continue;
		ALLOC_GROW(list.entry, list.nr + 1, list.alloc);
		list.entry[list.nr].name = xstrdup(ce->name);
		list.entry[list.nr].is_submodule = S_ISGITLINK(ce->ce_mode);
		if (list.entry[list.nr++].is_submodule &&
		    !is_staging_gitmodules_ok(the_repository->index))
			die(_("please stage your changes to .gitmodules or stash them to proceed"));
	}

	if (pathspec.nr) {
		const char *original;
		int seen_any = 0;
		char *skip_worktree_seen = NULL;
		struct string_list only_match_skip_worktree = STRING_LIST_INIT_NODUP;

		for (i = 0; i < pathspec.nr; i++) {
			original = pathspec.items[i].original;
			if (seen[i])
				seen_any = 1;
			else if (ignore_unmatch)
				continue;
			else if (!include_sparse &&
				 matches_skip_worktree(&pathspec, i, &skip_worktree_seen))
				string_list_append(&only_match_skip_worktree, original);
			else
				die(_("pathspec '%s' did not match any files"), original);

			if (!recursive && seen[i] == MATCHED_RECURSIVELY)
				die(_("not removing '%s' recursively without -r"),
				    *original ? original : ".");

			/* Check each path for safety concerns */
			if (safety_check_path(&rm_safety_state, pathspec.items[i].match)) {
				has_safety_concerns = 1;
			}
		}

		if (only_match_skip_worktree.nr) {
			advise_on_updating_sparse_paths(&only_match_skip_worktree);
			return 1;
		}
		free(skip_worktree_seen);
		string_list_clear(&only_match_skip_worktree, 0);

		if (!seen_any)
			exit(1);
	}
	clear_pathspec(&pathspec);
	free(seen);

	if (!index_only)
		submodules_absorb_gitdir_if_needed();

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
		if (repo_get_oid(the_repository, "HEAD", &oid))
			oidclr(&oid, the_repository->hash_algo);
		if (check_local_mod(&oid, index_only))
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

		if (remove_file_from_index(the_repository->index, path))
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
		int flag = force ? REMOVE_DIR_PURGE_ORIGINAL_CWD : 0;
		for (i = 0; i < list.nr; i++) {
			const char *path = list.entry[i].name;
			if (list.entry[i].is_submodule) {
				strbuf_reset(&buf);
				strbuf_addstr(&buf, path);
				if (remove_dir_recursively(&buf, flag))
					die(_("could not remove '%s'"), path);

				removed = 1;
				if (!remove_path_from_gitmodules(path))
					gitmodules_modified = 1;
				continue;
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
			stage_updated_gitmodules(the_repository->index);
	}

	if (has_safety_concerns) {
		const char *op_desc = recursive ? 
			"recursively remove files" : 
			"remove files";
			
		if (!safety_confirm_operation(&rm_safety_state, op_desc)) {
			ret = 1;
			goto cleanup;
		}
	}

	if (write_locked_index(the_repository->index, &lock_file,
			       COMMIT_LOCK | SKIP_IF_UNCHANGED))
		die(_("Unable to write new index file"));

cleanup:
	safety_clear(&rm_safety_state);
	return ret;
}
