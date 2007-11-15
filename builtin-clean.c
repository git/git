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

static int force = -1; /* unset */

static const char *const builtin_clean_usage[] = {
	"git-clean [-d] [-f] [-n] [-q] [-x | -X] [--] <paths>...",
	NULL
};

static int git_clean_config(const char *var, const char *value)
{
	if (!strcmp(var, "clean.requireforce"))
		force = !git_config_bool(var, value);
	return git_default_config(var, value);
}

int cmd_clean(int argc, const char **argv, const char *prefix)
{
	int j;
	int show_only = 0, remove_directories = 0, quiet = 0, ignored = 0;
	int ignored_only = 0, baselen = 0, config_set = 0;
	struct strbuf directory;
	struct dir_struct dir;
	const char *path, *base;
	static const char **pathspec;
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

	git_config(git_clean_config);
	if (force < 0)
		force = 0;
	else
		config_set = 1;

	argc = parse_options(argc, argv, options, builtin_clean_usage, 0);

	memset(&dir, 0, sizeof(dir));
	if (ignored_only)
		dir.show_ignored = 1;

	if (ignored && ignored_only)
		die("-x and -X cannot be used together");

	if (!show_only && !force)
		die("clean.requireForce%s set and -n or -f not given; "
		    "refusing to clean", config_set ? "" : " not");

	dir.show_other_directories = 1;

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
	strbuf_init(&directory, 0);

	for (j = 0; j < dir.nr; ++j) {
		struct dir_entry *ent = dir.entries[j];
		int len, pos, specs;
		struct cache_entry *ce;
		struct stat st;
		char *seen;

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

		if (!lstat(ent->name, &st) && (S_ISDIR(st.st_mode))) {
			int matched_path = 0;
			strbuf_addstr(&directory, ent->name);
			if (pathspec) {
				for (specs =0; pathspec[specs]; ++specs)
					/* nothing */;
				seen = xcalloc(specs, 1);
				/* Check if directory was explictly passed as
				 * pathspec.  If so we want to remove it */
				if (match_pathspec(pathspec, ent->name, ent->len,
						   baselen, seen))
					matched_path = 1;
				free(seen);
			}
			if (show_only && (remove_directories || matched_path)) {
				printf("Would remove %s\n", directory.buf);
			} else if (quiet && (remove_directories || matched_path)) {
				remove_dir_recursively(&directory, 0);
			} else if (remove_directories || matched_path) {
				printf("Removing %s\n", directory.buf);
				remove_dir_recursively(&directory, 0);
			} else if (show_only) {
				printf("Would not remove %s\n", directory.buf);
			} else {
				printf("Not removing %s\n", directory.buf);
			}
			strbuf_reset(&directory);
		} else {
			if (show_only) {
				printf("Would remove %s\n", ent->name);
				continue;
			} else if (!quiet) {
				printf("Removing %s\n", ent->name);
			}
			unlink(ent->name);
		}
	}

	strbuf_release(&directory);
	return 0;
}
