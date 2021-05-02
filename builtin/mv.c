/*
 * "git mv" builtin command
 *
 * Copyright (C) 2006 Johannes Schindelin
 */
#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "builtin.h"
#include "config.h"
#include "pathspec.h"
#include "lockfile.h"
#include "dir.h"
#include "cache-tree.h"
#include "string-list.h"
#include "parse-options.h"
#include "submodule.h"

static const char * const builtin_mv_usage[] = {
	N_("git mv [<options>] <source>... <destination>"),
	NULL
};

#define DUP_BASENAME 1
#define KEEP_TRAILING_SLASH 2

static const char **internal_prefix_pathspec(const char *prefix,
					     const char **pathspec,
					     int count, unsigned flags)
{
	int i;
	const char **result;
	int prefixlen = prefix ? strlen(prefix) : 0;
	ALLOC_ARRAY(result, count + 1);

	/* Create an intermediate copy of the pathspec based on the flags */
	for (i = 0; i < count; i++) {
		int length = strlen(pathspec[i]);
		int to_copy = length;
		char *it;
		while (!(flags & KEEP_TRAILING_SLASH) &&
		       to_copy > 0 && is_dir_sep(pathspec[i][to_copy - 1]))
			to_copy--;

		it = xmemdupz(pathspec[i], to_copy);
		if (flags & DUP_BASENAME) {
			result[i] = xstrdup(basename(it));
			free(it);
		} else {
			result[i] = it;
		}
	}
	result[count] = NULL;

	/* Prefix the pathspec and free the old intermediate strings */
	for (i = 0; i < count; i++) {
		const char *match = prefix_path(prefix, prefixlen, result[i]);
		free((char *) result[i]);
		result[i] = match;
	}

	return result;
}

static const char *add_slash(const char *path)
{
	size_t len = strlen(path);
	if (path[len - 1] != '/') {
		char *with_slash = xmalloc(st_add(len, 2));
		memcpy(with_slash, path, len);
		with_slash[len++] = '/';
		with_slash[len] = 0;
		return with_slash;
	}
	return path;
}

#define SUBMODULE_WITH_GITDIR ((const char *)1)

static void prepare_move_submodule(const char *src, int first,
				   const char **submodule_gitfile)
{
	struct strbuf submodule_dotgit = STRBUF_INIT;
	if (!S_ISGITLINK(active_cache[first]->ce_mode))
		die(_("Directory %s is in index and no submodule?"), src);
	if (!is_staging_gitmodules_ok(&the_index))
		die(_("Please stage your changes to .gitmodules or stash them to proceed"));
	strbuf_addf(&submodule_dotgit, "%s/.git", src);
	*submodule_gitfile = read_gitfile(submodule_dotgit.buf);
	if (*submodule_gitfile)
		*submodule_gitfile = xstrdup(*submodule_gitfile);
	else
		*submodule_gitfile = SUBMODULE_WITH_GITDIR;
	strbuf_release(&submodule_dotgit);
}

static int index_range_of_same_dir(const char *src, int length,
				   int *first_p, int *last_p)
{
	const char *src_w_slash = add_slash(src);
	int first, last, len_w_slash = length + 1;

	first = cache_name_pos(src_w_slash, len_w_slash);
	if (first >= 0)
		die(_("%.*s is in index"), len_w_slash, src_w_slash);

	first = -1 - first;
	for (last = first; last < active_nr; last++) {
		const char *path = active_cache[last]->name;
		if (strncmp(path, src_w_slash, len_w_slash))
			break;
	}
	if (src_w_slash != src)
		free((char *)src_w_slash);
	*first_p = first;
	*last_p = last;
	return last - first;
}

int cmd_mv(int argc, const char **argv, const char *prefix)
{
	int i, flags, gitmodules_modified = 0;
	int verbose = 0, show_only = 0, force = 0, ignore_errors = 0;
	struct option builtin_mv_options[] = {
		OPT__VERBOSE(&verbose, N_("be verbose")),
		OPT__DRY_RUN(&show_only, N_("dry run")),
		OPT__FORCE(&force, N_("force move/rename even if target exists"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_BOOL('k', NULL, &ignore_errors, N_("skip move/rename errors")),
		OPT_END(),
	};
	const char **source, **destination, **dest_path, **submodule_gitfile;
	enum update_mode { BOTH = 0, WORKING_DIRECTORY, INDEX } *modes;
	struct stat st;
	struct string_list src_for_dst = STRING_LIST_INIT_NODUP;
	struct lock_file lock_file = LOCK_INIT;
	struct cache_entry *ce;

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_mv_options,
			     builtin_mv_usage, 0);
	if (--argc < 1)
		usage_with_options(builtin_mv_usage, builtin_mv_options);

	hold_locked_index(&lock_file, LOCK_DIE_ON_ERROR);
	if (read_cache() < 0)
		die(_("index file corrupt"));

	source = internal_prefix_pathspec(prefix, argv, argc, 0);
	modes = xcalloc(argc, sizeof(enum update_mode));
	/*
	 * Keep trailing slash, needed to let
	 * "git mv file no-such-dir/" error out, except in the case
	 * "git mv directory no-such-dir/".
	 */
	flags = KEEP_TRAILING_SLASH;
	if (argc == 1 && is_directory(argv[0]) && !is_directory(argv[1]))
		flags = 0;
	dest_path = internal_prefix_pathspec(prefix, argv + argc, 1, flags);
	submodule_gitfile = xcalloc(argc, sizeof(char *));

	if (dest_path[0][0] == '\0')
		/* special case: "." was normalized to "" */
		destination = internal_prefix_pathspec(dest_path[0], argv, argc, DUP_BASENAME);
	else if (!lstat(dest_path[0], &st) &&
			S_ISDIR(st.st_mode)) {
		dest_path[0] = add_slash(dest_path[0]);
		destination = internal_prefix_pathspec(dest_path[0], argv, argc, DUP_BASENAME);
	} else {
		if (argc != 1)
			die(_("destination '%s' is not a directory"), dest_path[0]);
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
			int first = cache_name_pos(src, length), last;

			if (first >= 0)
				prepare_move_submodule(src, first,
						       submodule_gitfile + i);
			else if (index_range_of_same_dir(src, length,
							 &first, &last) < 1)
				bad = _("source directory is empty");
			else { /* last - first >= 1 */
				int j, dst_len, n;

				modes[i] = WORKING_DIRECTORY;
				n = argc + last - first;
				REALLOC_ARRAY(source, n);
				REALLOC_ARRAY(destination, n);
				REALLOC_ARRAY(modes, n);
				REALLOC_ARRAY(submodule_gitfile, n);

				dst = add_slash(dst);
				dst_len = strlen(dst);

				for (j = 0; j < last - first; j++) {
					const char *path = active_cache[first + j]->name;
					source[argc + j] = path;
					destination[argc + j] =
						prefix_path(dst, dst_len, path + length + 1);
					modes[argc + j] = INDEX;
					submodule_gitfile[argc + j] = NULL;
				}
				argc += last - first;
			}
		} else if (!(ce = cache_file_exists(src, length, 0))) {
			bad = _("not under version control");
		} else if (ce_stage(ce)) {
			bad = _("conflicted");
		} else if (lstat(dst, &st) == 0 &&
			 (!ignore_case || strcasecmp(src, dst))) {
			bad = _("destination exists");
			if (force) {
				/*
				 * only files can overwrite each other:
				 * check both source and destination
				 */
				if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
					if (verbose)
						warning(_("overwriting '%s'"), dst);
					bad = NULL;
				} else
					bad = _("Cannot overwrite");
			}
		} else if (string_list_has_string(&src_for_dst, dst))
			bad = _("multiple sources for the same target");
		else if (is_dir_sep(dst[strlen(dst) - 1]))
			bad = _("destination directory does not exist");
		else
			string_list_insert(&src_for_dst, dst);

		if (!bad)
			continue;
		if (!ignore_errors)
			die(_("%s, source=%s, destination=%s"),
			     bad, src, dst);
		if (--argc > 0) {
			int n = argc - i;
			memmove(source + i, source + i + 1,
				n * sizeof(char *));
			memmove(destination + i, destination + i + 1,
				n * sizeof(char *));
			memmove(modes + i, modes + i + 1,
				n * sizeof(enum update_mode));
			memmove(submodule_gitfile + i, submodule_gitfile + i + 1,
				n * sizeof(char *));
			i--;
		}
	}

	for (i = 0; i < argc; i++) {
		const char *src = source[i], *dst = destination[i];
		enum update_mode mode = modes[i];
		int pos;
		if (show_only || verbose)
			printf(_("Renaming %s to %s\n"), src, dst);
		if (show_only)
			continue;
		if (mode != INDEX && rename(src, dst) < 0) {
			if (ignore_errors)
				continue;
			die_errno(_("renaming '%s' failed"), src);
		}
		if (submodule_gitfile[i]) {
			if (!update_path_in_gitmodules(src, dst))
				gitmodules_modified = 1;
			if (submodule_gitfile[i] != SUBMODULE_WITH_GITDIR)
				connect_work_tree_and_git_dir(dst,
							      submodule_gitfile[i],
							      1);
		}

		if (mode == WORKING_DIRECTORY)
			continue;

		pos = cache_name_pos(src, strlen(src));
		assert(pos >= 0);
		rename_cache_entry_at(pos, dst);
	}

	if (gitmodules_modified)
		stage_updated_gitmodules(&the_index);

	if (write_locked_index(&the_index, &lock_file,
			       COMMIT_LOCK | SKIP_IF_UNCHANGED))
		die(_("Unable to write new index file"));

	string_list_clear(&src_for_dst, 0);
	UNLEAK(source);
	UNLEAK(dest_path);
	free(submodule_gitfile);
	free(modes);
	return 0;
}
