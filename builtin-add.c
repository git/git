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
#include "diff.h"
#include "diffcore.h"
#include "commit.h"
#include "revision.h"
#include "run-command.h"
#include "parse-options.h"

static const char * const builtin_add_usage[] = {
	"git-add [options] [--] <filepattern>...",
	NULL
};
static int patch_interactive = 0, add_interactive = 0;
static int take_worktree_changes;

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
		if (match_pathspec(pathspec, entry->name, entry->len,
				   prefix, seen))
			*dst++ = entry;
	}
	dir->nr = dst - dir->entries;

	for (i = 0; i < specs; i++) {
		if (!seen[i] && !file_exists(pathspec[i]))
			die("pathspec '%s' did not match any files",
					pathspec[i]);
	}
        free(seen);
}

static void fill_directory(struct dir_struct *dir, const char **pathspec,
		int ignored_too)
{
	const char *path, *base;
	int baselen;

	/* Set up the default git porcelain excludes */
	memset(dir, 0, sizeof(*dir));
	if (!ignored_too) {
		dir->collect_ignored = 1;
		setup_standard_excludes(dir);
	}

	/*
	 * Calculate common prefix for the pathspec, and
	 * use that to optimize the directory walk
	 */
	baselen = common_prefix(pathspec);
	path = ".";
	base = "";
	if (baselen)
		path = base = xmemdupz(*pathspec, baselen);

	/* Read the directory and prune it */
	read_directory(dir, path, base, baselen, pathspec);
	if (pathspec)
		prune_directory(dir, pathspec, baselen);
}

static void update_callback(struct diff_queue_struct *q,
			    struct diff_options *opt, void *cbdata)
{
	int i, verbose;

	verbose = *((int *)cbdata);
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		const char *path = p->one->path;
		switch (p->status) {
		default:
			die("unexpected diff status %c", p->status);
		case DIFF_STATUS_UNMERGED:
		case DIFF_STATUS_MODIFIED:
		case DIFF_STATUS_TYPE_CHANGED:
			add_file_to_cache(path, verbose);
			break;
		case DIFF_STATUS_DELETED:
			remove_file_from_cache(path);
			if (verbose)
				printf("remove '%s'\n", path);
			break;
		}
	}
}

void add_files_to_cache(int verbose, const char *prefix, const char **pathspec)
{
	struct rev_info rev;
	init_revisions(&rev, prefix);
	setup_revisions(0, NULL, &rev, NULL);
	rev.prune_data = pathspec;
	rev.diffopt.output_format = DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = update_callback;
	rev.diffopt.format_callback_data = &verbose;
	run_diff_files(&rev, DIFF_RACY_IS_MODIFIED);
}

static void refresh(int verbose, const char **pathspec)
{
	char *seen;
	int i, specs;

	for (specs = 0; pathspec[specs];  specs++)
		/* nothing */;
	seen = xcalloc(specs, 1);
	if (read_cache() < 0)
		die("index file corrupt");
	refresh_index(&the_index, verbose ? 0 : REFRESH_QUIET, pathspec, seen);
	for (i = 0; i < specs; i++) {
		if (!seen[i])
			die("pathspec '%s' did not match any files", pathspec[i]);
	}
        free(seen);
}

static const char **validate_pathspec(int argc, const char **argv, const char *prefix)
{
	const char **pathspec = get_pathspec(prefix, argv);

	return pathspec;
}

int interactive_add(int argc, const char **argv, const char *prefix)
{
	int status, ac;
	const char **args;
	const char **pathspec = NULL;

	if (argc) {
		pathspec = validate_pathspec(argc, argv, prefix);
		if (!pathspec)
			return -1;
	}

	args = xcalloc(sizeof(const char *), (argc + 4));
	ac = 0;
	args[ac++] = "add--interactive";
	if (patch_interactive)
		args[ac++] = "--patch";
	args[ac++] = "--";
	if (argc) {
		memcpy(&(args[ac]), pathspec, sizeof(const char *) * argc);
		ac += argc;
	}
	args[ac] = NULL;

	status = run_command_v_opt(args, RUN_GIT_CMD);
	free(args);
	return status;
}

static struct lock_file lock_file;

static const char ignore_error[] =
"The following paths are ignored by one of your .gitignore files:\n";

static int verbose = 0, show_only = 0, ignored_too = 0, refresh_only = 0;

static struct option builtin_add_options[] = {
	OPT__DRY_RUN(&show_only),
	OPT__VERBOSE(&verbose),
	OPT_GROUP(""),
	OPT_BOOLEAN('i', "interactive", &add_interactive, "interactive picking"),
	OPT_BOOLEAN('p', "patch", &patch_interactive, "interactive patching"),
	OPT_BOOLEAN('f', NULL, &ignored_too, "allow adding otherwise ignored files"),
	OPT_BOOLEAN('u', NULL, &take_worktree_changes, "update tracked files"),
	OPT_BOOLEAN( 0 , "refresh", &refresh_only, "don't add, only refresh the index"),
	OPT_END(),
};

int cmd_add(int argc, const char **argv, const char *prefix)
{
	int i, newfd;
	const char **pathspec;
	struct dir_struct dir;

	argc = parse_options(argc, argv, builtin_add_options,
			  builtin_add_usage, 0);
	if (patch_interactive)
		add_interactive = 1;
	if (add_interactive)
		exit(interactive_add(argc, argv, prefix));

	git_config(git_default_config);

	newfd = hold_locked_index(&lock_file, 1);

	if (take_worktree_changes) {
		const char **pathspec;
		if (read_cache() < 0)
			die("index file corrupt");
		pathspec = get_pathspec(prefix, argv);
		add_files_to_cache(verbose, prefix, pathspec);
		goto finish;
	}

	if (argc == 0) {
		fprintf(stderr, "Nothing specified, nothing added.\n");
		fprintf(stderr, "Maybe you wanted to say 'git add .'?\n");
		return 0;
	}
	pathspec = get_pathspec(prefix, argv);

	if (refresh_only) {
		refresh(verbose, pathspec);
		goto finish;
	}

	if (*argv) {
		/* Was there an invalid path? */
		if (pathspec) {
			int num;
			for (num = 0; pathspec[num]; num++)
				; /* just counting */
			if (argc != num)
				exit(1); /* error message already given */
		} else
			exit(1); /* error message already given */
	}

	fill_directory(&dir, pathspec, ignored_too);

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

	if (read_cache() < 0)
		die("index file corrupt");

	if (dir.ignored_nr) {
		fprintf(stderr, ignore_error);
		for (i = 0; i < dir.ignored_nr; i++) {
			fprintf(stderr, "%s\n", dir.ignored[i]->name);
		}
		fprintf(stderr, "Use -f if you really want to add them.\n");
		die("no files added");
	}

	for (i = 0; i < dir.nr; i++)
		add_file_to_cache(dir.entries[i]->name, verbose);

 finish:
	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_locked_index(&lock_file))
			die("Unable to write new index file");
	}

	return 0;
}
