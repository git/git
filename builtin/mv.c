/*
 * "git mv" builtin command
 *
 * Copyright (C) 2006 Johannes Schindelin
 */
#include "cache.h"
#include "builtin.h"
#include "dir.h"
#include "cache-tree.h"
#include "string-list.h"
#include "parse-options.h"

static const char * const builtin_mv_usage[] = {
	"git mv [options] <source>... <destination>",
	NULL
};

static const char **copy_pathspec(const char *prefix, const char **pathspec,
				  int count, int base_name)
{
	int i;
	const char **result = xmalloc((count + 1) * sizeof(const char *));
	memcpy(result, pathspec, count * sizeof(const char *));
	result[count] = NULL;
	for (i = 0; i < count; i++) {
		int length = strlen(result[i]);
		int to_copy = length;
		while (to_copy > 0 && is_dir_sep(result[i][to_copy - 1]))
			to_copy--;
		if (to_copy != length || base_name) {
			char *it = xmemdupz(result[i], to_copy);
			if (base_name) {
				result[i] = xstrdup(basename(it));
				free(it);
			} else
				result[i] = it;
		}
	}
	return get_pathspec(prefix, result);
}

static const char *add_slash(const char *path)
{
	int len = strlen(path);
	if (path[len - 1] != '/') {
		char *with_slash = xmalloc(len + 2);
		memcpy(with_slash, path, len);
		with_slash[len++] = '/';
		with_slash[len] = 0;
		return with_slash;
	}
	return path;
}

static struct lock_file lock_file;

int cmd_mv(int argc, const char **argv, const char *prefix)
{
	int i, newfd;
	int verbose = 0, show_only = 0, force = 0, ignore_errors = 0;
	struct option builtin_mv_options[] = {
		OPT__DRY_RUN(&show_only, "dry run"),
		OPT__FORCE(&force, "force move/rename even if target exists"),
		OPT_BOOLEAN('k', NULL, &ignore_errors, "skip move/rename errors"),
		OPT_END(),
	};
	const char **source, **destination, **dest_path;
	enum update_mode { BOTH = 0, WORKING_DIRECTORY, INDEX } *modes;
	struct stat st;
	struct string_list src_for_dst = STRING_LIST_INIT_NODUP;

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_mv_options,
			     builtin_mv_usage, 0);
	if (--argc < 1)
		usage_with_options(builtin_mv_usage, builtin_mv_options);

	newfd = hold_locked_index(&lock_file, 1);
	if (read_cache() < 0)
		die(_("index file corrupt"));

	source = copy_pathspec(prefix, argv, argc, 0);
	modes = xcalloc(argc, sizeof(enum update_mode));
	dest_path = copy_pathspec(prefix, argv + argc, 1, 0);

	if (dest_path[0][0] == '\0')
		/* special case: "." was normalized to "" */
		destination = copy_pathspec(dest_path[0], argv, argc, 1);
	else if (!lstat(dest_path[0], &st) &&
			S_ISDIR(st.st_mode)) {
		dest_path[0] = add_slash(dest_path[0]);
		destination = copy_pathspec(dest_path[0], argv, argc, 1);
	} else {
		if (argc != 1)
			usage_with_options(builtin_mv_usage, builtin_mv_options);
		destination = dest_path;
	}

	/* Checking */
	for (i = 0; i < argc; i++) {
		const char *src = source[i], *dst = destination[i];
		int length, src_is_dir;
		const char *bad = NULL;

		if (show_only)
			printf(_("Checking rename of '%s' to '%s'\n"), src, dst);

		length = strlen(src);
		if (lstat(src, &st) < 0)
			bad = _("bad source");
		else if (!strncmp(src, dst, length) &&
				(dst[length] == 0 || dst[length] == '/')) {
			bad = _("can not move directory into itself");
		} else if ((src_is_dir = S_ISDIR(st.st_mode))
				&& lstat(dst, &st) == 0)
			bad = _("cannot move directory over file");
		else if (src_is_dir) {
			const char *src_w_slash = add_slash(src);
			int len_w_slash = length + 1;
			int first, last;

			modes[i] = WORKING_DIRECTORY;

			first = cache_name_pos(src_w_slash, len_w_slash);
			if (first >= 0)
				die (_("Huh? %.*s is in index?"),
						len_w_slash, src_w_slash);

			first = -1 - first;
			for (last = first; last < active_nr; last++) {
				const char *path = active_cache[last]->name;
				if (strncmp(path, src_w_slash, len_w_slash))
					break;
			}
			free((char *)src_w_slash);

			if (last - first < 1)
				bad = _("source directory is empty");
			else {
				int j, dst_len;

				if (last - first > 0) {
					source = xrealloc(source,
							(argc + last - first)
							* sizeof(char *));
					destination = xrealloc(destination,
							(argc + last - first)
							* sizeof(char *));
					modes = xrealloc(modes,
							(argc + last - first)
							* sizeof(enum update_mode));
				}

				dst = add_slash(dst);
				dst_len = strlen(dst);

				for (j = 0; j < last - first; j++) {
					const char *path =
						active_cache[first + j]->name;
					source[argc + j] = path;
					destination[argc + j] =
						prefix_path(dst, dst_len,
							path + length + 1);
					modes[argc + j] = INDEX;
				}
				argc += last - first;
			}
		} else if (cache_name_pos(src, length) < 0)
			bad = _("not under version control");
		else if (lstat(dst, &st) == 0) {
			bad = _("destination exists");
			if (force) {
				/*
				 * only files can overwrite each other:
				 * check both source and destination
				 */
				if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
					warning(_("%s; will overwrite!"), bad);
					bad = NULL;
				} else
					bad = _("Cannot overwrite");
			}
		} else if (string_list_has_string(&src_for_dst, dst))
			bad = _("multiple sources for the same target");
		else
			string_list_insert(&src_for_dst, dst);

		if (bad) {
			if (ignore_errors) {
				if (--argc > 0) {
					memmove(source + i, source + i + 1,
						(argc - i) * sizeof(char *));
					memmove(destination + i,
						destination + i + 1,
						(argc - i) * sizeof(char *));
					i--;
				}
			} else
				die (_("%s, source=%s, destination=%s"),
				     bad, src, dst);
		}
	}

	for (i = 0; i < argc; i++) {
		const char *src = source[i], *dst = destination[i];
		enum update_mode mode = modes[i];
		int pos;
		if (show_only || verbose)
			printf(_("Renaming %s to %s\n"), src, dst);
		if (!show_only && mode != INDEX &&
				rename(src, dst) < 0 && !ignore_errors)
			die_errno (_("renaming '%s' failed"), src);

		if (mode == WORKING_DIRECTORY)
			continue;

		pos = cache_name_pos(src, strlen(src));
		assert(pos >= 0);
		if (!show_only)
			rename_cache_entry_at(pos, dst);
	}

	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_locked_index(&lock_file))
			die(_("Unable to write new index file"));
	}

	return 0;
}
