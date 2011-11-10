/*
 * "git clean" builtin command
 *
 * Copyright (C) 2007 Shawn Bohrer
 *
 * Based on git-clean.sh by Pavel Roskin
 */

#include "builtin.h"
#include "cache.h"
#include "dir.h"
#include "parse-options.h"
#include "quote.h"

static int force = -1; /* unset */

static const char *const builtin_clean_usage[] = {
	"git clean [-d] [-f] [-n] [-q] [-x | -X] [--] <paths>...",
	NULL
};

static int git_clean_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "clean.requireforce"))
		force = !git_config_bool(var, value);
	return git_default_config(var, value, cb);
}

int cmd_clean(int argc, const char **argv, const char *prefix)
{
	int i;
	int show_only = 0, remove_directories = 0, quiet = 0, ignored = 0;
	int ignored_only = 0, baselen = 0, config_set = 0, errors = 0;
	struct strbuf directory = STRBUF_INIT;
	struct dir_struct dir;
	const char *path, *base;
	static const char **pathspec;
	struct strbuf buf = STRBUF_INIT;
	const char *qname;
	char *seen = NULL;
	struct option options[] = {
		OPT__QUIET(&quiet),
		OPT__DRY_RUN(&show_only),
		OPT_BOOLEAN('f', NULL, &force, "force"),
		OPT_BOOLEAN('d', NULL, &remove_directories,
				"remove whole directories"),
		OPT_BOOLEAN('x', NULL, &ignored, "remove ignored files, too"),
		OPT_BOOLEAN('X', NULL, &ignored_only,
				"remove only ignored files"),
		OPT_END()
	};

	git_config(git_clean_config, NULL);
	if (force < 0)
		force = 0;
	else
		config_set = 1;

	argc = parse_options(argc, argv, options, builtin_clean_usage, 0);

	memset(&dir, 0, sizeof(dir));
	if (ignored_only)
		dir.flags |= DIR_SHOW_IGNORED;

	if (ignored && ignored_only)
		die("-x and -X cannot be used together");

	if (!show_only && !force)
		die("clean.requireForce%s set and -n or -f not given; "
		    "refusing to clean", config_set ? "" : " not");

	dir.flags |= DIR_SHOW_OTHER_DIRECTORIES;

	if (!ignored)
		setup_standard_excludes(&dir);

	pathspec = get_pathspec(prefix, argv);
	read_cache();

	/*
	 * Calculate common prefix for the pathspec, and
	 * use that to optimize the directory walk
	 */
	baselen = common_prefix(pathspec);
	path = ".";
	base = "";
	if (baselen)
		path = base = xmemdupz(*pathspec, baselen);
	read_directory(&dir, path, base, baselen, pathspec);

	if (pathspec)
		seen = xmalloc(argc > 0 ? argc : 1);

	for (i = 0; i < dir.nr; i++) {
		struct dir_entry *ent = dir.entries[i];
		int len, pos;
		int matches = 0;
		struct cache_entry *ce;
		struct stat st;

		/*
		 * Remove the '/' at the end that directory
		 * walking adds for directory entries.
		 */
		len = ent->len;
		if (len && ent->name[len-1] == '/')
			len--;
		pos = cache_name_pos(ent->name, len);
		if (0 <= pos)
			continue;	/* exact match */
		pos = -pos - 1;
		if (pos < active_nr) {
			ce = active_cache[pos];
			if (ce_namelen(ce) == len &&
			    !memcmp(ce->name, ent->name, len))
				continue; /* Yup, this one exists unmerged */
		}

		/*
		 * we might have removed this as part of earlier
		 * recursive directory removal, so lstat() here could
		 * fail with ENOENT.
		 */
		if (lstat(ent->name, &st))
			continue;

		if (pathspec) {
			memset(seen, 0, argc > 0 ? argc : 1);
			matches = match_pathspec(pathspec, ent->name, len,
						 baselen, seen);
		}

		if (S_ISDIR(st.st_mode)) {
			strbuf_addstr(&directory, ent->name);
			qname = quote_path_relative(directory.buf, directory.len, &buf, prefix);
			if (show_only && (remove_directories ||
			    (matches == MATCHED_EXACTLY))) {
				printf("Would remove %s\n", qname);
			} else if (remove_directories ||
				   (matches == MATCHED_EXACTLY)) {
				if (!quiet)
					printf("Removing %s\n", qname);
				if (remove_dir_recursively(&directory, 0) != 0) {
					warning("failed to remove '%s'", qname);
					errors++;
				}
			} else if (show_only) {
				printf("Would not remove %s\n", qname);
			} else {
				printf("Not removing %s\n", qname);
			}
			strbuf_reset(&directory);
		} else {
			if (pathspec && !matches)
				continue;
			qname = quote_path_relative(ent->name, -1, &buf, prefix);
			if (show_only) {
				printf("Would remove %s\n", qname);
				continue;
			} else if (!quiet) {
				printf("Removing %s\n", qname);
			}
			if (unlink(ent->name) != 0) {
				warning("failed to remove '%s'", qname);
				errors++;
			}
		}
	}
	free(seen);

	strbuf_release(&directory);
	return (errors != 0);
}
