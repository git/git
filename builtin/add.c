/*
 * "git add" builtin command
 *
 * Copyright (C) 2006 Linus Torvalds
 */
#include "cache.h"
#include "builtin.h"
#include "dir.h"
#include "exec_cmd.h"
#include "cache-tree.h"
#include "run-command.h"
#include "parse-options.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"

static const char * const builtin_add_usage[] = {
	"git add [options] [--] <filepattern>...",
	NULL
};
static int patch_interactive, add_interactive, edit_interactive;
static int take_worktree_changes;

struct update_callback_data
{
	int flags;
	int add_errors;
};

static void update_callback(struct diff_queue_struct *q,
			    struct diff_options *opt, void *cbdata)
{
	int i;
	struct update_callback_data *data = cbdata;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		const char *path = p->one->path;
		switch (p->status) {
		default:
			die("unexpected diff status %c", p->status);
		case DIFF_STATUS_UNMERGED:
			/*
			 * ADD_CACHE_IGNORE_REMOVAL is unset if "git
			 * add -u" is calling us, In such a case, a
			 * missing work tree file needs to be removed
			 * if there is an unmerged entry at stage #2,
			 * but such a diff record is followed by
			 * another with DIFF_STATUS_DELETED (and if
			 * there is no stage #2, we won't see DELETED
			 * nor MODIFIED).  We can simply continue
			 * either way.
			 */
			if (!(data->flags & ADD_CACHE_IGNORE_REMOVAL))
				continue;
			/*
			 * Otherwise, it is "git add path" is asking
			 * to explicitly add it; we fall through.  A
			 * missing work tree file is an error and is
			 * caught by add_file_to_index() in such a
			 * case.
			 */
		case DIFF_STATUS_MODIFIED:
		case DIFF_STATUS_TYPE_CHANGED:
			if (add_file_to_index(&the_index, path, data->flags)) {
				if (!(data->flags & ADD_CACHE_IGNORE_ERRORS))
					die("updating files failed");
				data->add_errors++;
			}
			break;
		case DIFF_STATUS_DELETED:
			if (data->flags & ADD_CACHE_IGNORE_REMOVAL)
				break;
			if (!(data->flags & ADD_CACHE_PRETEND))
				remove_file_from_index(&the_index, path);
			if (data->flags & (ADD_CACHE_PRETEND|ADD_CACHE_VERBOSE))
				printf("remove '%s'\n", path);
			break;
		}
	}
}

int add_files_to_cache(const char *prefix, const char **pathspec, int flags)
{
	struct update_callback_data data;
	struct rev_info rev;
	init_revisions(&rev, prefix);
	setup_revisions(0, NULL, &rev, NULL);
	rev.prune_data = pathspec;
	rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = update_callback;
	data.flags = flags;
	data.add_errors = 0;
	rev.diffopt.format_callback_data = &data;
	run_diff_files(&rev, DIFF_RACY_IS_MODIFIED);
	return !!data.add_errors;
}

static void fill_pathspec_matches(const char **pathspec, char *seen, int specs)
{
	int num_unmatched = 0, i;

	/*
	 * Since we are walking the index as if we were walking the directory,
	 * we have to mark the matched pathspec as seen; otherwise we will
	 * mistakenly think that the user gave a pathspec that did not match
	 * anything.
	 */
	for (i = 0; i < specs; i++)
		if (!seen[i])
			num_unmatched++;
	if (!num_unmatched)
		return;
	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		match_pathspec(pathspec, ce->name, ce_namelen(ce), 0, seen);
	}
}

static char *find_used_pathspec(const char **pathspec)
{
	char *seen;
	int i;

	for (i = 0; pathspec[i];  i++)
		; /* just counting */
	seen = xcalloc(i, 1);
	fill_pathspec_matches(pathspec, seen, i);
	return seen;
}

static char *prune_directory(struct dir_struct *dir, const char **pathspec, int prefix)
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
		if (match_pathspec(pathspec, entry->name, entry->len,
				   prefix, seen))
			*dst++ = entry;
	}
	dir->nr = dst - dir->entries;
	fill_pathspec_matches(pathspec, seen, specs);
	return seen;
}

static void treat_gitlinks(const char **pathspec)
{
	int i;

	if (!pathspec || !*pathspec)
		return;

	for (i = 0; i < active_nr; i++) {
		struct cache_entry *ce = active_cache[i];
		if (S_ISGITLINK(ce->ce_mode)) {
			int len = ce_namelen(ce), j;
			for (j = 0; pathspec[j]; j++) {
				int len2 = strlen(pathspec[j]);
				if (len2 <= len || pathspec[j][len] != '/' ||
				    memcmp(ce->name, pathspec[j], len))
					continue;
				if (len2 == len + 1)
					/* strip trailing slash */
					pathspec[j] = xstrndup(ce->name, len);
				else
					die ("Path '%s' is in submodule '%.*s'",
						pathspec[j], len, ce->name);
			}
		}
	}
}

static void refresh(int verbose, const char **pathspec)
{
	char *seen;
	int i, specs;

	for (specs = 0; pathspec[specs];  specs++)
		/* nothing */;
	seen = xcalloc(specs, 1);
	refresh_index(&the_index, verbose ? REFRESH_IN_PORCELAIN : REFRESH_QUIET,
		      pathspec, seen, "Unstaged changes after refreshing the index:");
	for (i = 0; i < specs; i++) {
		if (!seen[i])
			die("pathspec '%s' did not match any files", pathspec[i]);
	}
        free(seen);
}

static const char **validate_pathspec(int argc, const char **argv, const char *prefix)
{
	const char **pathspec = get_pathspec(prefix, argv);

	if (pathspec) {
		const char **p;
		for (p = pathspec; *p; p++) {
			if (has_symlink_leading_path(*p, strlen(*p))) {
				int len = prefix ? strlen(prefix) : 0;
				die("'%s' is beyond a symbolic link", *p + len);
			}
		}
	}

	return pathspec;
}

int run_add_interactive(const char *revision, const char *patch_mode,
			const char **pathspec)
{
	int status, ac, pc = 0;
	const char **args;

	if (pathspec)
		while (pathspec[pc])
			pc++;

	args = xcalloc(sizeof(const char *), (pc + 5));
	ac = 0;
	args[ac++] = "add--interactive";
	if (patch_mode)
		args[ac++] = patch_mode;
	if (revision)
		args[ac++] = revision;
	args[ac++] = "--";
	if (pc) {
		memcpy(&(args[ac]), pathspec, sizeof(const char *) * pc);
		ac += pc;
	}
	args[ac] = NULL;

	status = run_command_v_opt(args, RUN_GIT_CMD);
	free(args);
	return status;
}

int interactive_add(int argc, const char **argv, const char *prefix)
{
	const char **pathspec = NULL;

	if (argc) {
		pathspec = validate_pathspec(argc, argv, prefix);
		if (!pathspec)
			return -1;
	}

	return run_add_interactive(NULL,
				   patch_interactive ? "--patch" : NULL,
				   pathspec);
}

static int edit_patch(int argc, const char **argv, const char *prefix)
{
	char *file = xstrdup(git_path("ADD_EDIT.patch"));
	const char *apply_argv[] = { "apply", "--recount", "--cached",
		NULL, NULL };
	struct child_process child;
	struct rev_info rev;
	int out;
	struct stat st;

	apply_argv[3] = file;

	git_config(git_diff_basic_config, NULL); /* no "diff" UI options */

	if (read_cache() < 0)
		die ("Could not read the index");

	init_revisions(&rev, prefix);
	rev.diffopt.context = 7;

	argc = setup_revisions(argc, argv, &rev, NULL);
	rev.diffopt.output_format = DIFF_FORMAT_PATCH;
	out = open(file, O_CREAT | O_WRONLY, 0644);
	if (out < 0)
		die ("Could not open '%s' for writing.", file);
	rev.diffopt.file = xfdopen(out, "w");
	rev.diffopt.close_file = 1;
	if (run_diff_files(&rev, 0))
		die ("Could not write patch");

	launch_editor(file, NULL, NULL);

	if (stat(file, &st))
		die_errno("Could not stat '%s'", file);
	if (!st.st_size)
		die("Empty patch. Aborted.");

	memset(&child, 0, sizeof(child));
	child.git_cmd = 1;
	child.argv = apply_argv;
	if (run_command(&child))
		die ("Could not apply '%s'", file);

	unlink(file);
	return 0;
}

static struct lock_file lock_file;

static const char ignore_error[] =
"The following paths are ignored by one of your .gitignore files:\n";

static int verbose = 0, show_only = 0, ignored_too = 0, refresh_only = 0;
static int ignore_add_errors, addremove, intent_to_add, ignore_missing = 0;

static struct option builtin_add_options[] = {
	OPT__DRY_RUN(&show_only),
	OPT__VERBOSE(&verbose),
	OPT_GROUP(""),
	OPT_BOOLEAN('i', "interactive", &add_interactive, "interactive picking"),
	OPT_BOOLEAN('p', "patch", &patch_interactive, "interactive patching"),
	OPT_BOOLEAN('e', "edit", &edit_interactive, "edit current diff and apply"),
	OPT_BOOLEAN('f', "force", &ignored_too, "allow adding otherwise ignored files"),
	OPT_BOOLEAN('u', "update", &take_worktree_changes, "update tracked files"),
	OPT_BOOLEAN('N', "intent-to-add", &intent_to_add, "record only the fact that the path will be added later"),
	OPT_BOOLEAN('A', "all", &addremove, "add all, noticing removal of tracked files"),
	OPT_BOOLEAN( 0 , "refresh", &refresh_only, "don't add, only refresh the index"),
	OPT_BOOLEAN( 0 , "ignore-errors", &ignore_add_errors, "just skip files which cannot be added because of errors"),
	OPT_BOOLEAN( 0 , "ignore-missing", &ignore_missing, "check if - even missing - files are ignored in dry run"),
	OPT_END(),
};

static int add_config(const char *var, const char *value, void *cb)
{
	if (!strcasecmp(var, "add.ignoreerrors") ||
	    !strcasecmp(var, "add.ignore-errors")) {
		ignore_add_errors = git_config_bool(var, value);
		return 0;
	}
	return git_default_config(var, value, cb);
}

static int add_files(struct dir_struct *dir, int flags)
{
	int i, exit_status = 0;

	if (dir->ignored_nr) {
		fprintf(stderr, ignore_error);
		for (i = 0; i < dir->ignored_nr; i++)
			fprintf(stderr, "%s\n", dir->ignored[i]->name);
		fprintf(stderr, "Use -f if you really want to add them.\n");
		die("no files added");
	}

	for (i = 0; i < dir->nr; i++)
		if (add_file_to_cache(dir->entries[i]->name, flags)) {
			if (!ignore_add_errors)
				die("adding files failed");
			exit_status = 1;
		}
	return exit_status;
}

int cmd_add(int argc, const char **argv, const char *prefix)
{
	int exit_status = 0;
	int newfd;
	const char **pathspec;
	struct dir_struct dir;
	int flags;
	int add_new_files;
	int require_pathspec;
	char *seen = NULL;

	git_config(add_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_add_options,
			  builtin_add_usage, PARSE_OPT_KEEP_ARGV0);
	if (patch_interactive)
		add_interactive = 1;
	if (add_interactive)
		exit(interactive_add(argc - 1, argv + 1, prefix));

	if (edit_interactive)
		return(edit_patch(argc, argv, prefix));
	argc--;
	argv++;

	if (addremove && take_worktree_changes)
		die("-A and -u are mutually incompatible");
	if (!show_only && ignore_missing)
		die("Option --ignore-missing can only be used together with --dry-run");
	if ((addremove || take_worktree_changes) && !argc) {
		static const char *here[2] = { ".", NULL };
		argc = 1;
		argv = here;
	}

	add_new_files = !take_worktree_changes && !refresh_only;
	require_pathspec = !take_worktree_changes;

	newfd = hold_locked_index(&lock_file, 1);

	flags = ((verbose ? ADD_CACHE_VERBOSE : 0) |
		 (show_only ? ADD_CACHE_PRETEND : 0) |
		 (intent_to_add ? ADD_CACHE_INTENT : 0) |
		 (ignore_add_errors ? ADD_CACHE_IGNORE_ERRORS : 0) |
		 (!(addremove || take_worktree_changes)
		  ? ADD_CACHE_IGNORE_REMOVAL : 0));

	if (require_pathspec && argc == 0) {
		fprintf(stderr, "Nothing specified, nothing added.\n");
		fprintf(stderr, "Maybe you wanted to say 'git add .'?\n");
		return 0;
	}
	pathspec = validate_pathspec(argc, argv, prefix);

	if (read_cache() < 0)
		die("index file corrupt");
	treat_gitlinks(pathspec);

	if (add_new_files) {
		int baselen;

		/* Set up the default git porcelain excludes */
		memset(&dir, 0, sizeof(dir));
		if (!ignored_too) {
			dir.flags |= DIR_COLLECT_IGNORED;
			setup_standard_excludes(&dir);
		}

		/* This picks up the paths that are not tracked */
		baselen = fill_directory(&dir, pathspec);
		if (pathspec)
			seen = prune_directory(&dir, pathspec, baselen);
	}

	if (refresh_only) {
		refresh(verbose, pathspec);
		goto finish;
	}

	if (pathspec) {
		int i;
		if (!seen)
			seen = find_used_pathspec(pathspec);
		for (i = 0; pathspec[i]; i++) {
			if (!seen[i] && pathspec[i][0]
			    && !file_exists(pathspec[i])) {
				if (ignore_missing) {
					if (excluded(&dir, pathspec[i], DT_UNKNOWN))
						dir_add_ignored(&dir, pathspec[i], strlen(pathspec[i]));
				} else
					die("pathspec '%s' did not match any files",
					    pathspec[i]);
			}
		}
		free(seen);
	}

	exit_status |= add_files_to_cache(prefix, pathspec, flags);

	if (add_new_files)
		exit_status |= add_files(&dir, flags);

 finish:
	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_locked_index(&lock_file))
			die("Unable to write new index file");
	}

	return exit_status;
}
