/*
 * "git add" builtin command
 *
 * Copyright (C) 2006 Linus Torvalds
 */
#include "cache.h"
#include "builtin.h"
#include "dir.h"
#include "pathspec.h"
#include "exec_cmd.h"
#include "cache-tree.h"
#include "run-command.h"
#include "parse-options.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "bulk-checkin.h"

static const char * const builtin_add_usage[] = {
	N_("git add [options] [--] <pathspec>..."),
	NULL
};
static int patch_interactive, add_interactive, edit_interactive;
static int take_worktree_changes;

struct update_callback_data {
	int flags;
	int add_errors;
	const char *implicit_dot;
	size_t implicit_dot_len;

	/* only needed for 2.0 transition preparation */
	int warn_add_would_remove;
};

static const char *option_with_implicit_dot;
static const char *short_option_with_implicit_dot;

static void warn_pathless_add(void)
{
	static int shown;
	assert(option_with_implicit_dot && short_option_with_implicit_dot);

	if (shown)
		return;
	shown = 1;

	/*
	 * To be consistent with "git add -p" and most Git
	 * commands, we should default to being tree-wide, but
	 * this is not the original behavior and can't be
	 * changed until users trained themselves not to type
	 * "git add -u" or "git add -A". For now, we warn and
	 * keep the old behavior. Later, the behavior can be changed
	 * to tree-wide, keeping the warning for a while, and
	 * eventually we can drop the warning.
	 */
	warning(_("The behavior of 'git add %s (or %s)' with no path argument from a\n"
		  "subdirectory of the tree will change in Git 2.0 and should not be used anymore.\n"
		  "To add content for the whole tree, run:\n"
		  "\n"
		  "  git add %s :/\n"
		  "  (or git add %s :/)\n"
		  "\n"
		  "To restrict the command to the current directory, run:\n"
		  "\n"
		  "  git add %s .\n"
		  "  (or git add %s .)\n"
		  "\n"
		  "With the current Git version, the command is restricted to "
		  "the current directory.\n"
		  ""),
		option_with_implicit_dot, short_option_with_implicit_dot,
		option_with_implicit_dot, short_option_with_implicit_dot,
		option_with_implicit_dot, short_option_with_implicit_dot);
}

static int fix_unmerged_status(struct diff_filepair *p,
			       struct update_callback_data *data)
{
	if (p->status != DIFF_STATUS_UNMERGED)
		return p->status;
	if (!(data->flags & ADD_CACHE_IGNORE_REMOVAL) && !p->two->mode)
		/*
		 * This is not an explicit add request, and the
		 * path is missing from the working tree (deleted)
		 */
		return DIFF_STATUS_DELETED;
	else
		/*
		 * Either an explicit add request, or path exists
		 * in the working tree.  An attempt to explicitly
		 * add a path that does not exist in the working tree
		 * will be caught as an error by the caller immediately.
		 */
		return DIFF_STATUS_MODIFIED;
}

static const char *add_would_remove_warning = N_(
	"You ran 'git add' with neither '-A (--all)' or '--ignore-removal',\n"
"whose behaviour will change in Git 2.0 with respect to paths you removed.\n"
"Paths like '%s' that are\n"
"removed from your working tree are ignored with this version of Git.\n"
"\n"
"* 'git add --ignore-removal <pathspec>', which is the current default,\n"
"  ignores paths you removed from your working tree.\n"
"\n"
"* 'git add --all <pathspec>' will let you also record the removals.\n"
"\n"
"Run 'git status' to check the paths you removed from your working tree.\n");

static void warn_add_would_remove(const char *path)
{
	warning(_(add_would_remove_warning), path);
}

static void update_callback(struct diff_queue_struct *q,
			    struct diff_options *opt, void *cbdata)
{
	int i;
	struct update_callback_data *data = cbdata;
	const char *implicit_dot = data->implicit_dot;
	size_t implicit_dot_len = data->implicit_dot_len;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		const char *path = p->one->path;
		/*
		 * Check if "git add -A" or "git add -u" was run from a
		 * subdirectory with a modified file outside that directory,
		 * and warn if so.
		 *
		 * "git add -u" will behave like "git add -u :/" instead of
		 * "git add -u ." in the future.  This warning prepares for
		 * that change.
		 */
		if (implicit_dot &&
		    strncmp_icase(path, implicit_dot, implicit_dot_len)) {
			warn_pathless_add();
			continue;
		}
		switch (fix_unmerged_status(p, data)) {
		default:
			die(_("unexpected diff status %c"), p->status);
		case DIFF_STATUS_MODIFIED:
		case DIFF_STATUS_TYPE_CHANGED:
			if (add_file_to_index(&the_index, path, data->flags)) {
				if (!(data->flags & ADD_CACHE_IGNORE_ERRORS))
					die(_("updating files failed"));
				data->add_errors++;
			}
			break;
		case DIFF_STATUS_DELETED:
			if (data->warn_add_would_remove) {
				warn_add_would_remove(path);
				data->warn_add_would_remove = 0;
			}
			if (data->flags & ADD_CACHE_IGNORE_REMOVAL)
				break;
			if (!(data->flags & ADD_CACHE_PRETEND))
				remove_file_from_index(&the_index, path);
			if (data->flags & (ADD_CACHE_PRETEND|ADD_CACHE_VERBOSE))
				printf(_("remove '%s'\n"), path);
			break;
		}
	}
}

static void update_files_in_cache(const char *prefix,
				  const struct pathspec *pathspec,
				  struct update_callback_data *data)
{
	struct rev_info rev;

	init_revisions(&rev, prefix);
	setup_revisions(0, NULL, &rev, NULL);
	if (pathspec)
		copy_pathspec(&rev.prune_data, pathspec);
	rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = update_callback;
	rev.diffopt.format_callback_data = data;
	rev.max_count = 0; /* do not compare unmerged paths with stage #2 */
	run_diff_files(&rev, DIFF_RACY_IS_MODIFIED);
}

int add_files_to_cache(const char *prefix,
		       const struct pathspec *pathspec, int flags)
{
	struct update_callback_data data;

	memset(&data, 0, sizeof(data));
	data.flags = flags;
	update_files_in_cache(prefix, pathspec, &data);
	return !!data.add_errors;
}

#define WARN_IMPLICIT_DOT (1u << 0)
static char *prune_directory(struct dir_struct *dir, struct pathspec *pathspec,
			     int prefix, unsigned flag)
{
	char *seen;
	int i;
	struct dir_entry **src, **dst;

	seen = xcalloc(pathspec->nr, 1);

	src = dst = dir->entries;
	i = dir->nr;
	while (--i >= 0) {
		struct dir_entry *entry = *src++;
		if (match_pathspec_depth(pathspec, entry->name, entry->len,
					 prefix, seen))
			*dst++ = entry;
		else if (flag & WARN_IMPLICIT_DOT)
			/*
			 * "git add -A" was run from a subdirectory with a
			 * new file outside that directory.
			 *
			 * "git add -A" will behave like "git add -A :/"
			 * instead of "git add -A ." in the future.
			 * Warn about the coming behavior change.
			 */
			warn_pathless_add();
	}
	dir->nr = dst - dir->entries;
	add_pathspec_matches_against_index(pathspec, seen);
	return seen;
}

static void refresh(int verbose, const struct pathspec *pathspec)
{
	char *seen;
	int i;

	seen = xcalloc(pathspec->nr, 1);
	refresh_index(&the_index, verbose ? REFRESH_IN_PORCELAIN : REFRESH_QUIET,
		      pathspec, seen, _("Unstaged changes after refreshing the index:"));
	for (i = 0; i < pathspec->nr; i++) {
		if (!seen[i])
			die(_("pathspec '%s' did not match any files"),
			    pathspec->items[i].match);
	}
        free(seen);
}

int run_add_interactive(const char *revision, const char *patch_mode,
			const struct pathspec *pathspec)
{
	int status, ac, i;
	const char **args;

	args = xcalloc(sizeof(const char *), (pathspec->nr + 6));
	ac = 0;
	args[ac++] = "add--interactive";
	if (patch_mode)
		args[ac++] = patch_mode;
	if (revision)
		args[ac++] = revision;
	args[ac++] = "--";
	for (i = 0; i < pathspec->nr; i++)
		/* pass original pathspec, to be re-parsed */
		args[ac++] = pathspec->items[i].original;

	status = run_command_v_opt(args, RUN_GIT_CMD);
	free(args);
	return status;
}

int interactive_add(int argc, const char **argv, const char *prefix, int patch)
{
	struct pathspec pathspec;

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_FULL |
		       PATHSPEC_SYMLINK_LEADING_PATH |
		       PATHSPEC_PREFIX_ORIGIN,
		       prefix, argv);

	return run_add_interactive(NULL,
				   patch ? "--patch" : NULL,
				   &pathspec);
}

static int edit_patch(int argc, const char **argv, const char *prefix)
{
	char *file = git_pathdup("ADD_EDIT.patch");
	const char *apply_argv[] = { "apply", "--recount", "--cached",
		NULL, NULL };
	struct child_process child;
	struct rev_info rev;
	int out;
	struct stat st;

	apply_argv[3] = file;

	git_config(git_diff_basic_config, NULL); /* no "diff" UI options */

	if (read_cache() < 0)
		die(_("Could not read the index"));

	init_revisions(&rev, prefix);
	rev.diffopt.context = 7;

	argc = setup_revisions(argc, argv, &rev, NULL);
	rev.diffopt.output_format = DIFF_FORMAT_PATCH;
	rev.diffopt.use_color = 0;
	DIFF_OPT_SET(&rev.diffopt, IGNORE_DIRTY_SUBMODULES);
	out = open(file, O_CREAT | O_WRONLY, 0666);
	if (out < 0)
		die(_("Could not open '%s' for writing."), file);
	rev.diffopt.file = xfdopen(out, "w");
	rev.diffopt.close_file = 1;
	if (run_diff_files(&rev, 0))
		die(_("Could not write patch"));

	launch_editor(file, NULL, NULL);

	if (stat(file, &st))
		die_errno(_("Could not stat '%s'"), file);
	if (!st.st_size)
		die(_("Empty patch. Aborted."));

	memset(&child, 0, sizeof(child));
	child.git_cmd = 1;
	child.argv = apply_argv;
	if (run_command(&child))
		die(_("Could not apply '%s'"), file);

	unlink(file);
	free(file);
	return 0;
}

static struct lock_file lock_file;

static const char ignore_error[] =
N_("The following paths are ignored by one of your .gitignore files:\n");

static int verbose, show_only, ignored_too, refresh_only;
static int ignore_add_errors, intent_to_add, ignore_missing;

#define ADDREMOVE_DEFAULT 0 /* Change to 1 in Git 2.0 */
static int addremove = ADDREMOVE_DEFAULT;
static int addremove_explicit = -1; /* unspecified */

static int ignore_removal_cb(const struct option *opt, const char *arg, int unset)
{
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
	OPT__FORCE(&ignored_too, N_("allow adding otherwise ignored files")),
	OPT_BOOL('u', "update", &take_worktree_changes, N_("update tracked files")),
	OPT_BOOL('N', "intent-to-add", &intent_to_add, N_("record only the fact that the path will be added later")),
	OPT_BOOL('A', "all", &addremove_explicit, N_("add changes from all tracked and untracked files")),
	{ OPTION_CALLBACK, 0, "ignore-removal", &addremove_explicit,
	  NULL /* takes no arguments */,
	  N_("ignore paths removed in the working tree (same as --no-all)"),
	  PARSE_OPT_NOARG, ignore_removal_cb },
	OPT_BOOL( 0 , "refresh", &refresh_only, N_("don't add, only refresh the index")),
	OPT_BOOL( 0 , "ignore-errors", &ignore_add_errors, N_("just skip files which cannot be added because of errors")),
	OPT_BOOL( 0 , "ignore-missing", &ignore_missing, N_("check if - even missing - files are ignored in dry run")),
	OPT_END(),
};

static int add_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "add.ignoreerrors") ||
	    !strcmp(var, "add.ignore-errors")) {
		ignore_add_errors = git_config_bool(var, value);
		return 0;
	}
	return git_default_config(var, value, cb);
}

static int add_files(struct dir_struct *dir, int flags)
{
	int i, exit_status = 0;

	if (dir->ignored_nr) {
		fprintf(stderr, _(ignore_error));
		for (i = 0; i < dir->ignored_nr; i++)
			fprintf(stderr, "%s\n", dir->ignored[i]->name);
		fprintf(stderr, _("Use -f if you really want to add them.\n"));
		die(_("no files added"));
	}

	for (i = 0; i < dir->nr; i++)
		if (add_file_to_cache(dir->entries[i]->name, flags)) {
			if (!ignore_add_errors)
				die(_("adding files failed"));
			exit_status = 1;
		}
	return exit_status;
}

int cmd_add(int argc, const char **argv, const char *prefix)
{
	int exit_status = 0;
	int newfd;
	struct pathspec pathspec;
	struct dir_struct dir;
	int flags;
	int add_new_files;
	int require_pathspec;
	char *seen = NULL;
	int implicit_dot = 0;
	struct update_callback_data update_data;

	git_config(add_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_add_options,
			  builtin_add_usage, PARSE_OPT_KEEP_ARGV0);
	if (patch_interactive)
		add_interactive = 1;
	if (add_interactive)
		exit(interactive_add(argc - 1, argv + 1, prefix, patch_interactive));

	if (edit_interactive)
		return(edit_patch(argc, argv, prefix));
	argc--;
	argv++;

	if (0 <= addremove_explicit)
		addremove = addremove_explicit;
	else if (take_worktree_changes && ADDREMOVE_DEFAULT)
		addremove = 0; /* "-u" was given but not "-A" */

	if (addremove && take_worktree_changes)
		die(_("-A and -u are mutually incompatible"));

	/*
	 * Warn when "git add pathspec..." was given without "-u" or "-A"
	 * and pathspec... covers a removed path.
	 */
	memset(&update_data, 0, sizeof(update_data));
	if (!take_worktree_changes && addremove_explicit < 0)
		update_data.warn_add_would_remove = 1;

	if (!take_worktree_changes && addremove_explicit < 0 && argc)
		/*
		 * Turn "git add pathspec..." to "git add -A pathspec..."
		 * in Git 2.0 but not yet
		 */
		; /* addremove = 1; */

	if (!show_only && ignore_missing)
		die(_("Option --ignore-missing can only be used together with --dry-run"));
	if (addremove) {
		option_with_implicit_dot = "--all";
		short_option_with_implicit_dot = "-A";
	}
	if (take_worktree_changes) {
		option_with_implicit_dot = "--update";
		short_option_with_implicit_dot = "-u";
	}
	if (option_with_implicit_dot && !argc) {
		static const char *here[2] = { ".", NULL };
		argc = 1;
		argv = here;
		implicit_dot = 1;
	}

	add_new_files = !take_worktree_changes && !refresh_only;
	require_pathspec = !take_worktree_changes;

	newfd = hold_locked_index(&lock_file, 1);

	flags = ((verbose ? ADD_CACHE_VERBOSE : 0) |
		 (show_only ? ADD_CACHE_PRETEND : 0) |
		 (intent_to_add ? ADD_CACHE_INTENT : 0) |
		 (ignore_add_errors ? ADD_CACHE_IGNORE_ERRORS : 0) |
		 (!(addremove || take_worktree_changes)
		  ? ADD_CACHE_IGNORE_REMOVAL : 0)) |
		 (implicit_dot ? ADD_CACHE_IMPLICIT_DOT : 0);

	if (require_pathspec && argc == 0) {
		fprintf(stderr, _("Nothing specified, nothing added.\n"));
		fprintf(stderr, _("Maybe you wanted to say 'git add .'?\n"));
		return 0;
	}

	if (read_cache() < 0)
		die(_("index file corrupt"));

	/*
	 * Check the "pathspec '%s' did not match any files" block
	 * below before enabling new magic.
	 */
	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_FULL |
		       PATHSPEC_SYMLINK_LEADING_PATH |
		       PATHSPEC_STRIP_SUBMODULE_SLASH_EXPENSIVE,
		       prefix, argv);

	if (add_new_files) {
		int baselen;
		struct pathspec empty_pathspec;

		/* Set up the default git porcelain excludes */
		memset(&dir, 0, sizeof(dir));
		if (!ignored_too) {
			dir.flags |= DIR_COLLECT_IGNORED;
			setup_standard_excludes(&dir);
		}

		memset(&empty_pathspec, 0, sizeof(empty_pathspec));
		/* This picks up the paths that are not tracked */
		baselen = fill_directory(&dir, implicit_dot ? &empty_pathspec : &pathspec);
		if (pathspec.nr)
			seen = prune_directory(&dir, &pathspec, baselen,
					implicit_dot ? WARN_IMPLICIT_DOT : 0);
	}

	if (refresh_only) {
		refresh(verbose, &pathspec);
		goto finish;
	}
	if (implicit_dot && prefix)
		refresh_cache(REFRESH_QUIET);

	if (pathspec.nr) {
		int i;

		if (!seen)
			seen = find_pathspecs_matching_against_index(&pathspec);

		/*
		 * file_exists() assumes exact match
		 */
		GUARD_PATHSPEC(&pathspec,
			       PATHSPEC_FROMTOP |
			       PATHSPEC_LITERAL |
			       PATHSPEC_GLOB |
			       PATHSPEC_ICASE);

		for (i = 0; i < pathspec.nr; i++) {
			const char *path = pathspec.items[i].match;
			if (!seen[i] &&
			    ((pathspec.items[i].magic &
			      (PATHSPEC_GLOB | PATHSPEC_ICASE)) ||
			     !file_exists(path))) {
				if (ignore_missing) {
					int dtype = DT_UNKNOWN;
					if (is_excluded(&dir, path, &dtype))
						dir_add_ignored(&dir, path, pathspec.items[i].len);
				} else
					die(_("pathspec '%s' did not match any files"),
					    pathspec.items[i].original);
			}
		}
		free(seen);
	}

	plug_bulk_checkin();

	if ((flags & ADD_CACHE_IMPLICIT_DOT) && prefix) {
		/*
		 * Check for modified files throughout the worktree so
		 * update_callback has a chance to warn about changes
		 * outside the cwd.
		 */
		update_data.implicit_dot = prefix;
		update_data.implicit_dot_len = strlen(prefix);
		free_pathspec(&pathspec);
		memset(&pathspec, 0, sizeof(pathspec));
	}
	update_data.flags = flags & ~ADD_CACHE_IMPLICIT_DOT;
	update_files_in_cache(prefix, &pathspec, &update_data);

	exit_status |= !!update_data.add_errors;
	if (add_new_files)
		exit_status |= add_files(&dir, flags);

	unplug_bulk_checkin();

finish:
	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_locked_index(&lock_file))
			die(_("Unable to write new index file"));
	}

	return exit_status;
}
