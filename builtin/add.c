/*
 * "git add" builtin command
 *
 * Copyright (C) 2006 Linus Torvalds
 */
#include "builtin.h"
#include "advice.h"
#include "config.h"
#include "lockfile.h"
#include "editor.h"
#include "dir.h"
#include "gettext.h"
#include "pathspec.h"
#include "run-command.h"
#include "parse-options.h"
#include "path.h"
#include "preload-index.h"
#include "diff.h"
#include "read-cache.h"
#include "revision.h"
#include "bulk-checkin.h"
#include "strvec.h"
#include "submodule.h"
#include "add-interactive.h"

static const char * const builtin_add_usage[] = {
	N_("git add [<options>] [--] <pathspec>..."),
	NULL
};
static int patch_interactive, add_interactive, edit_interactive;
static int take_worktree_changes;
static int add_renormalize;
static int pathspec_file_nul;
static int include_sparse;
static const char *pathspec_from_file;

static int chmod_pathspec(struct repository *repo,
			  struct pathspec *pathspec,
			  char flip,
			  int show_only)
{
	int i, ret = 0;

	for (i = 0; i < repo->index->cache_nr; i++) {
		struct cache_entry *ce = repo->index->cache[i];
		int err;

		if (!include_sparse &&
		    (ce_skip_worktree(ce) ||
		     !path_in_sparse_checkout(ce->name, repo->index)))
			continue;

		if (pathspec && !ce_path_match(repo->index, ce, pathspec, NULL))
			continue;

		if (!show_only)
			err = chmod_index_entry(repo->index, ce, flip);
		else
			err = S_ISREG(ce->ce_mode) ? 0 : -1;

		if (err < 0)
			ret = error(_("cannot chmod %cx '%s'"), flip, ce->name);
	}

	return ret;
}

static int renormalize_tracked_files(struct repository *repo,
				     const struct pathspec *pathspec,
				     int flags)
{
	int i, retval = 0;

	for (i = 0; i < repo->index->cache_nr; i++) {
		struct cache_entry *ce = repo->index->cache[i];

		if (!include_sparse &&
		    (ce_skip_worktree(ce) ||
		     !path_in_sparse_checkout(ce->name, repo->index)))
			continue;
		if (ce_stage(ce))
			continue; /* do not touch unmerged paths */
		if (!S_ISREG(ce->ce_mode) && !S_ISLNK(ce->ce_mode))
			continue; /* do not touch non blobs */
		if (pathspec && !ce_path_match(repo->index, ce, pathspec, NULL))
			continue;
		retval |= add_file_to_index(repo->index, ce->name,
					    flags | ADD_CACHE_RENORMALIZE);
	}

	return retval;
}

static char *prune_directory(struct repository *repo,
			     struct dir_struct *dir,
			     struct pathspec *pathspec,
			     int prefix)
{
	char *seen;
	int i;
	struct dir_entry **src, **dst;

	seen = xcalloc(pathspec->nr, 1);

	src = dst = dir->entries;
	i = dir->nr;
	while (--i >= 0) {
		struct dir_entry *entry = *src++;
		if (dir_path_match(repo->index, entry, pathspec, prefix, seen))
			*dst++ = entry;
	}
	dir->nr = dst - dir->entries;
	add_pathspec_matches_against_index(pathspec, repo->index, seen,
					   PS_IGNORE_SKIP_WORKTREE);
	return seen;
}

static int refresh(struct repository *repo, int verbose, const struct pathspec *pathspec)
{
	char *seen;
	int i, ret = 0;
	char *skip_worktree_seen = NULL;
	struct string_list only_match_skip_worktree = STRING_LIST_INIT_NODUP;
	unsigned int flags = REFRESH_IGNORE_SKIP_WORKTREE |
		    (verbose ? REFRESH_IN_PORCELAIN : REFRESH_QUIET);

	seen = xcalloc(pathspec->nr, 1);
	refresh_index(repo->index, flags, pathspec, seen,
		      _("Unstaged changes after refreshing the index:"));
	for (i = 0; i < pathspec->nr; i++) {
		if (!seen[i]) {
			const char *path = pathspec->items[i].original;

			if (matches_skip_worktree(pathspec, i, &skip_worktree_seen) ||
			    !path_in_sparse_checkout(path, repo->index)) {
				string_list_append(&only_match_skip_worktree,
						   pathspec->items[i].original);
			} else {
				die(_("pathspec '%s' did not match any files"),
				    pathspec->items[i].original);
			}
		}
	}

	if (only_match_skip_worktree.nr) {
		advise_on_updating_sparse_paths(&only_match_skip_worktree);
		ret = 1;
	}

	free(seen);
	free(skip_worktree_seen);
	string_list_clear(&only_match_skip_worktree, 0);
	return ret;
}

int interactive_add(struct repository *repo,
		    const char **argv,
		    const char *prefix,
		    int patch)
{
	struct pathspec pathspec;
	int ret;

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_FULL |
		       PATHSPEC_SYMLINK_LEADING_PATH |
		       PATHSPEC_PREFIX_ORIGIN,
		       prefix, argv);

	if (patch)
		ret = !!run_add_p(repo, ADD_P_ADD, NULL, &pathspec);
	else
		ret = !!run_add_i(repo, &pathspec);

	clear_pathspec(&pathspec);
	return ret;
}

static int edit_patch(struct repository *repo,
		      int argc,
		      const char **argv,
		      const char *prefix)
{
	char *file = repo_git_path(repo, "ADD_EDIT.patch");
	struct child_process child = CHILD_PROCESS_INIT;
	struct rev_info rev;
	int out;
	struct stat st;

	repo_config(repo, git_diff_basic_config, NULL);

	if (repo_read_index(repo) < 0)
		die(_("could not read the index"));

	repo_init_revisions(repo, &rev, prefix);
	rev.diffopt.context = 7;

	argc = setup_revisions(argc, argv, &rev, NULL);
	rev.diffopt.output_format = DIFF_FORMAT_PATCH;
	rev.diffopt.use_color = 0;
	rev.diffopt.flags.ignore_dirty_submodules = 1;
	out = xopen(file, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	rev.diffopt.file = xfdopen(out, "w");
	rev.diffopt.close_file = 1;
	run_diff_files(&rev, 0);

	if (launch_editor(file, NULL, NULL))
		die(_("editing patch failed"));

	if (stat(file, &st))
		die_errno(_("could not stat '%s'"), file);
	if (!st.st_size)
		die(_("empty patch. aborted"));

	child.git_cmd = 1;
	strvec_pushl(&child.args, "apply", "--recount", "--cached", file,
		     NULL);
	if (run_command(&child))
		die(_("could not apply '%s'"), file);

	unlink(file);
	free(file);
	release_revisions(&rev);
	return 0;
}

static const char ignore_error[] =
N_("The following paths are ignored by one of your .gitignore files:\n");

static int verbose, show_only, ignored_too, refresh_only;
static int ignore_add_errors, intent_to_add, ignore_missing;
static int warn_on_embedded_repo = 1;

#define ADDREMOVE_DEFAULT 1
static int addremove = ADDREMOVE_DEFAULT;
static int addremove_explicit = -1; /* unspecified */

static char *chmod_arg;

static int ignore_removal_cb(const struct option *opt, const char *arg, int unset)
{
	BUG_ON_OPT_ARG(arg);

	/* if we are told to ignore, we are not adding removals */
	*(int *)opt->value = !unset ? 0 : 1;
	return 0;
}

static struct option builtin_add_options[] = {
	OPT__DRY_RUN(&show_only, N_("dry run")),
	OPT__VERBOSE(&verbose, N_("be verbose")),
	OPT_GROUP(""),
	OPT_BOOL('i', "interactive", &add_interactive, N_("interactive picking")),
	OPT_BOOL('p', "patch", &patch_interactive, N_("select hunks interactively")),
	OPT_BOOL('e', "edit", &edit_interactive, N_("edit current diff and apply")),
	OPT__FORCE(&ignored_too, N_("allow adding otherwise ignored files"), 0),
	OPT_BOOL('u', "update", &take_worktree_changes, N_("update tracked files")),
	OPT_BOOL(0, "renormalize", &add_renormalize, N_("renormalize EOL of tracked files (implies -u)")),
	OPT_BOOL('N', "intent-to-add", &intent_to_add, N_("record only the fact that the path will be added later")),
	OPT_BOOL('A', "all", &addremove_explicit, N_("add changes from all tracked and untracked files")),
	OPT_CALLBACK_F(0, "ignore-removal", &addremove_explicit,
	  NULL /* takes no arguments */,
	  N_("ignore paths removed in the working tree (same as --no-all)"),
	  PARSE_OPT_NOARG, ignore_removal_cb),
	OPT_BOOL( 0 , "refresh", &refresh_only, N_("don't add, only refresh the index")),
	OPT_BOOL( 0 , "ignore-errors", &ignore_add_errors, N_("just skip files which cannot be added because of errors")),
	OPT_BOOL( 0 , "ignore-missing", &ignore_missing, N_("check if - even missing - files are ignored in dry run")),
	OPT_BOOL(0, "sparse", &include_sparse, N_("allow updating entries outside of the sparse-checkout cone")),
	OPT_STRING(0, "chmod", &chmod_arg, "(+|-)x",
		   N_("override the executable bit of the listed files")),
	OPT_HIDDEN_BOOL(0, "warn-embedded-repo", &warn_on_embedded_repo,
			N_("warn when adding an embedded repository")),
	OPT_PATHSPEC_FROM_FILE(&pathspec_from_file),
	OPT_PATHSPEC_FILE_NUL(&pathspec_file_nul),
	OPT_END(),
};

static int add_config(const char *var, const char *value,
		      const struct config_context *ctx, void *cb)
{
	if (!strcmp(var, "add.ignoreerrors") ||
	    !strcmp(var, "add.ignore-errors")) {
		ignore_add_errors = git_config_bool(var, value);
		return 0;
	}

	if (git_color_config(var, value, cb) < 0)
		return -1;

	return git_default_config(var, value, ctx, cb);
}

static const char embedded_advice[] = N_(
"You've added another git repository inside your current repository.\n"
"Clones of the outer repository will not contain the contents of\n"
"the embedded repository and will not know how to obtain it.\n"
"If you meant to add a submodule, use:\n"
"\n"
"	git submodule add <url> %s\n"
"\n"
"If you added this path by mistake, you can remove it from the\n"
"index with:\n"
"\n"
"	git rm --cached %s\n"
"\n"
"See \"git help submodule\" for more information."
);

static void check_embedded_repo(const char *path)
{
	struct strbuf name = STRBUF_INIT;
	static int adviced_on_embedded_repo = 0;

	if (!warn_on_embedded_repo)
		return;
	if (!ends_with(path, "/"))
		return;

	/* Drop trailing slash for aesthetics */
	strbuf_addstr(&name, path);
	strbuf_strip_suffix(&name, "/");

	warning(_("adding embedded git repository: %s"), name.buf);
	if (!adviced_on_embedded_repo) {
		advise_if_enabled(ADVICE_ADD_EMBEDDED_REPO,
				  embedded_advice, name.buf, name.buf);
		adviced_on_embedded_repo = 1;
	}

	strbuf_release(&name);
}

static int add_files(struct repository *repo, struct dir_struct *dir, int flags)
{
	int i, exit_status = 0;
	struct string_list matched_sparse_paths = STRING_LIST_INIT_NODUP;

	if (dir->ignored_nr) {
		fprintf(stderr, _(ignore_error));
		for (i = 0; i < dir->ignored_nr; i++)
			fprintf(stderr, "%s\n", dir->ignored[i]->name);
		advise_if_enabled(ADVICE_ADD_IGNORED_FILE,
				  _("Use -f if you really want to add them."));
		exit_status = 1;
	}

	for (i = 0; i < dir->nr; i++) {
		if (!include_sparse &&
		    !path_in_sparse_checkout(dir->entries[i]->name, repo->index)) {
			string_list_append(&matched_sparse_paths,
					   dir->entries[i]->name);
			continue;
		}
		if (add_file_to_index(repo->index, dir->entries[i]->name, flags)) {
			if (!ignore_add_errors)
				die(_("adding files failed"));
			exit_status = 1;
		} else {
			check_embedded_repo(dir->entries[i]->name);
		}
	}

	if (matched_sparse_paths.nr) {
		advise_on_updating_sparse_paths(&matched_sparse_paths);
		exit_status = 1;
	}

	string_list_clear(&matched_sparse_paths, 0);

	return exit_status;
}

int cmd_add(int argc,
	    const char **argv,
	    const char *prefix,
	    struct repository *repo)
{
	int exit_status = 0;
	struct pathspec pathspec;
	struct dir_struct dir = DIR_INIT;
	int flags;
	int add_new_files;
	int require_pathspec;
	char *seen = NULL;
	char *ps_matched = NULL;
	struct lock_file lock_file = LOCK_INIT;

	if (repo)
		repo_config(repo, add_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_add_options,
			  builtin_add_usage, PARSE_OPT_KEEP_ARGV0);
	if (patch_interactive)
		add_interactive = 1;
	if (add_interactive) {
		if (show_only)
			die(_("options '%s' and '%s' cannot be used together"), "--dry-run", "--interactive/--patch");
		if (pathspec_from_file)
			die(_("options '%s' and '%s' cannot be used together"), "--pathspec-from-file", "--interactive/--patch");
		exit(interactive_add(repo, argv + 1, prefix, patch_interactive));
	}

	if (edit_interactive) {
		if (pathspec_from_file)
			die(_("options '%s' and '%s' cannot be used together"), "--pathspec-from-file", "--edit");
		return(edit_patch(repo, argc, argv, prefix));
	}
	argc--;
	argv++;

	if (0 <= addremove_explicit)
		addremove = addremove_explicit;
	else if (take_worktree_changes && ADDREMOVE_DEFAULT)
		addremove = 0; /* "-u" was given but not "-A" */

	if (addremove && take_worktree_changes)
		die(_("options '%s' and '%s' cannot be used together"), "-A", "-u");

	if (!show_only && ignore_missing)
		die(_("the option '%s' requires '%s'"), "--ignore-missing", "--dry-run");

	if (chmod_arg && ((chmod_arg[0] != '-' && chmod_arg[0] != '+') ||
			  chmod_arg[1] != 'x' || chmod_arg[2]))
		die(_("--chmod param '%s' must be either -x or +x"), chmod_arg);

	add_new_files = !take_worktree_changes && !refresh_only && !add_renormalize;
	require_pathspec = !(take_worktree_changes || (0 < addremove_explicit));

	prepare_repo_settings(repo);
	repo->settings.command_requires_full_index = 0;

	repo_hold_locked_index(repo, &lock_file, LOCK_DIE_ON_ERROR);

	/*
	 * Check the "pathspec '%s' did not match any files" block
	 * below before enabling new magic.
	 */
	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_FULL |
		       PATHSPEC_SYMLINK_LEADING_PATH,
		       prefix, argv);

	if (pathspec_from_file) {
		if (pathspec.nr)
			die(_("'%s' and pathspec arguments cannot be used together"), "--pathspec-from-file");

		parse_pathspec_file(&pathspec, 0,
				    PATHSPEC_PREFER_FULL |
				    PATHSPEC_SYMLINK_LEADING_PATH,
				    prefix, pathspec_from_file, pathspec_file_nul);
	} else if (pathspec_file_nul) {
		die(_("the option '%s' requires '%s'"), "--pathspec-file-nul", "--pathspec-from-file");
	}

	if (require_pathspec && pathspec.nr == 0) {
		fprintf(stderr, _("Nothing specified, nothing added.\n"));
		advise_if_enabled(ADVICE_ADD_EMPTY_PATHSPEC,
				  _("Maybe you wanted to say 'git add .'?"));
		return 0;
	}

	if (!take_worktree_changes && addremove_explicit < 0 && pathspec.nr)
		/* Turn "git add pathspec..." to "git add -A pathspec..." */
		addremove = 1;

	flags = ((verbose ? ADD_CACHE_VERBOSE : 0) |
		 (show_only ? ADD_CACHE_PRETEND : 0) |
		 (intent_to_add ? ADD_CACHE_INTENT : 0) |
		 (ignore_add_errors ? ADD_CACHE_IGNORE_ERRORS : 0) |
		 (!(addremove || take_worktree_changes)
		  ? ADD_CACHE_IGNORE_REMOVAL : 0));

	if (repo_read_index_preload(repo, &pathspec, 0) < 0)
		die(_("index file corrupt"));

	die_in_unpopulated_submodule(repo->index, prefix);
	die_path_inside_submodule(repo->index, &pathspec);

	enable_fscache(1);
	/* We do not really re-read the index but update the up-to-date flags */
	preload_index(repo->index, &pathspec, 0);

	if (add_new_files) {
		int baselen;

		/* Set up the default git porcelain excludes */
		if (!ignored_too) {
			dir.flags |= DIR_COLLECT_IGNORED;
			setup_standard_excludes(&dir);
		}

		/* This picks up the paths that are not tracked */
		baselen = fill_directory(&dir, repo->index, &pathspec);
		if (pathspec.nr)
			seen = prune_directory(repo, &dir, &pathspec, baselen);
	}

	if (refresh_only) {
		exit_status |= refresh(repo, verbose, &pathspec);
		goto finish;
	}

	if (pathspec.nr) {
		int i;
		char *skip_worktree_seen = NULL;
		struct string_list only_match_skip_worktree = STRING_LIST_INIT_NODUP;

		if (!seen)
			seen = find_pathspecs_matching_against_index(&pathspec,
					repo->index, PS_IGNORE_SKIP_WORKTREE);

		/*
		 * file_exists() assumes exact match
		 */
		GUARD_PATHSPEC(&pathspec,
			       PATHSPEC_FROMTOP |
			       PATHSPEC_LITERAL |
			       PATHSPEC_GLOB |
			       PATHSPEC_ICASE |
			       PATHSPEC_EXCLUDE |
			       PATHSPEC_ATTR);

		for (i = 0; i < pathspec.nr; i++) {
			const char *path = pathspec.items[i].match;

			if (pathspec.items[i].magic & PATHSPEC_EXCLUDE)
				continue;
			if (seen[i])
				continue;

			if (!include_sparse &&
			    matches_skip_worktree(&pathspec, i, &skip_worktree_seen)) {
				string_list_append(&only_match_skip_worktree,
						   pathspec.items[i].original);
				continue;
			}

			/* Don't complain at 'git add .' on empty repo */
			if (!path[0])
				continue;

			if ((pathspec.items[i].magic & (PATHSPEC_GLOB | PATHSPEC_ICASE)) ||
			    !file_exists(path)) {
				if (ignore_missing) {
					int dtype = DT_UNKNOWN;
					if (is_excluded(&dir, repo->index, path, &dtype))
						dir_add_ignored(&dir, repo->index,
								path, pathspec.items[i].len);
				} else
					die(_("pathspec '%s' did not match any files"),
					    pathspec.items[i].original);
			}
		}


		if (only_match_skip_worktree.nr) {
			advise_on_updating_sparse_paths(&only_match_skip_worktree);
			exit_status = 1;
		}

		free(seen);
		free(skip_worktree_seen);
		string_list_clear(&only_match_skip_worktree, 0);
	}

	begin_odb_transaction();

	ps_matched = xcalloc(pathspec.nr, 1);
	if (add_renormalize)
		exit_status |= renormalize_tracked_files(repo, &pathspec, flags);
	else
		exit_status |= add_files_to_cache(repo, prefix,
						  &pathspec, ps_matched,
						  include_sparse, flags);

	if (take_worktree_changes && !add_renormalize && !ignore_add_errors &&
	    report_path_error(ps_matched, &pathspec))
		exit(128);

	if (add_new_files)
		exit_status |= add_files(repo, &dir, flags);

	if (chmod_arg && pathspec.nr)
		exit_status |= chmod_pathspec(repo, &pathspec, chmod_arg[0], show_only);
	end_odb_transaction();

finish:
	if (write_locked_index(repo->index, &lock_file,
			       COMMIT_LOCK | SKIP_IF_UNCHANGED))
		die(_("unable to write new index file"));

	free(ps_matched);
	dir_clear(&dir);
	clear_pathspec(&pathspec);
	enable_fscache(0);
	return exit_status;
}
