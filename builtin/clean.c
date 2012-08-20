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
#include "string-list.h"
#include "quote.h"

static int force = -1; /* unset */

static const char *const builtin_clean_usage[] = {
	N_("git clean [-d] [-f] [-n] [-q] [-e <pattern>] [-x | -X] [--] <paths>..."),
	NULL
};

static int git_clean_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "clean.requireforce"))
		force = !git_config_bool(var, value);
	return git_default_config(var, value, cb);
}

static int exclude_cb(const struct option *opt, const char *arg, int unset)
{
	struct string_list *exclude_list = opt->value;
	string_list_append(exclude_list, arg);
	return 0;
}

int cmd_clean(int argc, const char **argv, const char *prefix)
{
	int i;
	int show_only = 0, remove_directories = 0, quiet = 0, ignored = 0;
	int ignored_only = 0, config_set = 0, errors = 0;
	int rm_flags = REMOVE_DIR_KEEP_NESTED_GIT;
	struct strbuf directory = STRBUF_INIT;
	struct dir_struct dir;
	static const char **pathspec;
	struct strbuf buf = STRBUF_INIT;
	struct string_list exclude_list = STRING_LIST_INIT_NODUP;
	const char *qname;
	char *seen = NULL;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("do not print names of files removed")),
		OPT__DRY_RUN(&show_only, N_("dry run")),
		OPT__FORCE(&force, N_("force")),
		OPT_BOOLEAN('d', NULL, &remove_directories,
				N_("remove whole directories")),
		{ OPTION_CALLBACK, 'e', "exclude", &exclude_list, N_("pattern"),
		  N_("add <pattern> to ignore rules"), PARSE_OPT_NONEG, exclude_cb },
		OPT_BOOLEAN('x', NULL, &ignored, N_("remove ignored files, too")),
		OPT_BOOLEAN('X', NULL, &ignored_only,
				N_("remove only ignored files")),
		OPT_END()
	};

	git_config(git_clean_config, NULL);
	if (force < 0)
		force = 0;
	else
		config_set = 1;

	argc = parse_options(argc, argv, prefix, options, builtin_clean_usage,
			     0);

	memset(&dir, 0, sizeof(dir));
	if (ignored_only)
		dir.flags |= DIR_SHOW_IGNORED;

	if (ignored && ignored_only)
		die(_("-x and -X cannot be used together"));

	if (!show_only && !force) {
		if (config_set)
			die(_("clean.requireForce set to true and neither -n nor -f given; "
				  "refusing to clean"));
		else
			die(_("clean.requireForce defaults to true and neither -n nor -f given; "
				  "refusing to clean"));
	}

	if (force > 1)
		rm_flags = 0;

	dir.flags |= DIR_SHOW_OTHER_DIRECTORIES;

	if (read_cache() < 0)
		die(_("index file corrupt"));

	if (!ignored)
		setup_standard_excludes(&dir);

	for (i = 0; i < exclude_list.nr; i++)
		add_exclude(exclude_list.items[i].string, "", 0,
			    &dir.exclude_list[EXC_CMDL]);

	pathspec = get_pathspec(prefix, argv);

	fill_directory(&dir, pathspec);

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
						 0, seen);
		}

		if (S_ISDIR(st.st_mode)) {
			strbuf_addstr(&directory, ent->name);
			qname = quote_path_relative(directory.buf, directory.len, &buf, prefix);
			if (show_only && (remove_directories ||
			    (matches == MATCHED_EXACTLY))) {
				printf(_("Would remove %s\n"), qname);
			} else if (remove_directories ||
				   (matches == MATCHED_EXACTLY)) {
				if (!quiet)
					printf(_("Removing %s\n"), qname);
				if (remove_dir_recursively(&directory,
							   rm_flags) != 0) {
					warning(_("failed to remove %s"), qname);
					errors++;
				}
			} else if (show_only) {
				printf(_("Would not remove %s\n"), qname);
			} else {
				printf(_("Not removing %s\n"), qname);
			}
			strbuf_reset(&directory);
		} else {
			if (pathspec && !matches)
				continue;
			qname = quote_path_relative(ent->name, -1, &buf, prefix);
			if (show_only) {
				printf(_("Would remove %s\n"), qname);
				continue;
			} else if (!quiet) {
				printf(_("Removing %s\n"), qname);
			}
			if (unlink(ent->name) != 0) {
				warning(_("failed to remove %s"), qname);
				errors++;
			}
		}
	}
	free(seen);

	strbuf_release(&directory);
	string_list_clear(&exclude_list, 0);
	return (errors != 0);
}
