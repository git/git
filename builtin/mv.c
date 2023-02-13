/*
 * "git mv" builtin command
 *
 * Copyright (C) 2006 Johannes Schindelin
 */
#define USE_THE_INDEX_VARIABLE
#include "builtin.h"
#include "config.h"
#include "pathspec.h"
#include "lockfile.h"
#include "dir.h"
#include "cache-tree.h"
#include "string-list.h"
#include "parse-options.h"
#include "submodule.h"
#include "entry.h"

static const char * const builtin_mv_usage[] = {
	N_("git mv [<options>] <source>... <destination>"),
	NULL
};

enum update_mode {
	WORKING_DIRECTORY = (1 << 1),
	INDEX = (1 << 2),
	SPARSE = (1 << 3),
	SKIP_WORKTREE_DIR = (1 << 4),
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
	if (len && path[len - 1] != '/') {
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
	if (!S_ISGITLINK(the_index.cache[first]->ce_mode))
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

	first = index_name_pos(&the_index, src_w_slash, len_w_slash);
	if (first >= 0)
		die(_("%.*s is in index"), len_w_slash, src_w_slash);

	first = -1 - first;
	for (last = first; last < the_index.cache_nr; last++) {
		const char *path = the_index.cache[last]->name;
		if (strncmp(path, src_w_slash, len_w_slash))
			break;
	}
	if (src_w_slash != src)
		free((char *)src_w_slash);
	*first_p = first;
	*last_p = last;
	return last - first;
}

/*
 * Given the path of a directory that does not exist on-disk, check whether the
 * directory contains any entries in the index with the SKIP_WORKTREE flag
 * enabled.
 * Return 1 if such index entries exist.
 * Return 0 otherwise.
 */
static int empty_dir_has_sparse_contents(const char *name)
{
	int ret = 0;
	const char *with_slash = add_slash(name);
	int length = strlen(with_slash);

	int pos = index_name_pos(&the_index, with_slash, length);
	const struct cache_entry *ce;

	if (pos < 0) {
		pos = -pos - 1;
		if (pos >= the_index.cache_nr)
			goto free_return;
		ce = the_index.cache[pos];
		if (strncmp(with_slash, ce->name, length))
			goto free_return;
		if (ce_skip_worktree(ce))
			ret = 1;
	}

free_return:
	if (with_slash != name)
		free((char *)with_slash);
	return ret;
}

int cmd_mv(int argc, const char **argv, const char *prefix)
{
	int i, flags, gitmodules_modified = 0;
	int verbose = 0, show_only = 0, force = 0, ignore_errors = 0, ignore_sparse = 0;
	struct option builtin_mv_options[] = {
		OPT__VERBOSE(&verbose, N_("be verbose")),
		OPT__DRY_RUN(&show_only, N_("dry run")),
		OPT__FORCE(&force, N_("force move/rename even if target exists"),
			   PARSE_OPT_NOCOMPLETE),
		OPT_BOOL('k', NULL, &ignore_errors, N_("skip move/rename errors")),
		OPT_BOOL(0, "sparse", &ignore_sparse, N_("allow updating entries outside of the sparse-checkout cone")),
		OPT_END(),
	};
	const char **source, **destination, **dest_path, **submodule_gitfile;
	const char *dst_w_slash;
	const char **src_dir = NULL;
	int src_dir_nr = 0, src_dir_alloc = 0;
	struct strbuf a_src_dir = STRBUF_INIT;
	enum update_mode *modes, dst_mode = 0;
	struct stat st;
	struct string_list src_for_dst = STRING_LIST_INIT_NODUP;
	struct lock_file lock_file = LOCK_INIT;
	struct cache_entry *ce;
	struct string_list only_match_skip_worktree = STRING_LIST_INIT_NODUP;
	struct string_list dirty_paths = STRING_LIST_INIT_NODUP;

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, prefix, builtin_mv_options,
			     builtin_mv_usage, 0);
	if (--argc < 1)
		usage_with_options(builtin_mv_usage, builtin_mv_options);

	repo_hold_locked_index(the_repository, &lock_file, LOCK_DIE_ON_ERROR);
	if (repo_read_index(the_repository) < 0)
		die(_("index file corrupt"));

	source = internal_prefix_pathspec(prefix, argv, argc, 0);
	CALLOC_ARRAY(modes, argc);

	/*
	 * Keep trailing slash, needed to let
	 * "git mv file no-such-dir/" error out, except in the case
	 * "git mv directory no-such-dir/".
	 */
	flags = KEEP_TRAILING_SLASH;
	if (argc == 1 && is_directory(argv[0]) && !is_directory(argv[1]))
		flags = 0;
	dest_path = internal_prefix_pathspec(prefix, argv + argc, 1, flags);
	dst_w_slash = add_slash(dest_path[0]);
	submodule_gitfile = xcalloc(argc, sizeof(char *));

	if (dest_path[0][0] == '\0')
		/* special case: "." was normalized to "" */
		destination = internal_prefix_pathspec(dest_path[0], argv, argc, DUP_BASENAME);
	else if (!lstat(dest_path[0], &st) &&
			S_ISDIR(st.st_mode)) {
		destination = internal_prefix_pathspec(dst_w_slash, argv, argc, DUP_BASENAME);
	} else {
		if (!path_in_sparse_checkout(dst_w_slash, &the_index) &&
		    empty_dir_has_sparse_contents(dst_w_slash)) {
			destination = internal_prefix_pathspec(dst_w_slash, argv, argc, DUP_BASENAME);
			dst_mode = SKIP_WORKTREE_DIR;
		} else if (argc != 1) {
			die(_("destination '%s' is not a directory"), dest_path[0]);
		} else {
			destination = dest_path;
			/*
			 * <destination> is a file outside of sparse-checkout
			 * cone. Insist on cone mode here for backward
			 * compatibility. We don't want dst_mode to be assigned
			 * for a file when the repo is using no-cone mode (which
			 * is deprecated at this point) sparse-checkout. As
			 * SPARSE here is only considering cone-mode situation.
			 */
			if (!path_in_cone_mode_sparse_checkout(destination[0], &the_index))
				dst_mode = SPARSE;
		}
	}
	if (dst_w_slash != dest_path[0]) {
		free((char *)dst_w_slash);
		dst_w_slash = NULL;
	}

	/* Checking */
	for (i = 0; i < argc; i++) {
		const char *src = source[i], *dst = destination[i];
		int length;
		const char *bad = NULL;
		int skip_sparse = 0;

		if (show_only)
			printf(_("Checking rename of '%s' to '%s'\n"), src, dst);

		length = strlen(src);
		if (lstat(src, &st) < 0) {
			int pos;
			const struct cache_entry *ce;

			pos = index_name_pos(&the_index, src, length);
			if (pos < 0) {
				const char *src_w_slash = add_slash(src);
				if (!path_in_sparse_checkout(src_w_slash, &the_index) &&
				    empty_dir_has_sparse_contents(src)) {
					modes[i] |= SKIP_WORKTREE_DIR;
					goto dir_check;
				}
				/* only error if existence is expected. */
				if (!(modes[i] & SPARSE))
					bad = _("bad source");
				goto act_on_entry;
			}
			ce = the_index.cache[pos];
			if (!ce_skip_worktree(ce)) {
				bad = _("bad source");
				goto act_on_entry;
			}
			if (!ignore_sparse) {
				string_list_append(&only_match_skip_worktree, src);
				goto act_on_entry;
			}
			/* Check if dst exists in index */
			if (index_name_pos(&the_index, dst, strlen(dst)) < 0) {
				modes[i] |= SPARSE;
				goto act_on_entry;
			}
			if (!force) {
				bad = _("destination exists");
				goto act_on_entry;
			}
			modes[i] |= SPARSE;
			goto act_on_entry;
		}
		if (!strncmp(src, dst, length) &&
		    (dst[length] == 0 || dst[length] == '/')) {
			bad = _("can not move directory into itself");
			goto act_on_entry;
		}
		if (S_ISDIR(st.st_mode)
		    && lstat(dst, &st) == 0) {
			bad = _("cannot move directory over file");
			goto act_on_entry;
		}

dir_check:
		if (S_ISDIR(st.st_mode)) {
			int j, dst_len, n;
			int first = index_name_pos(&the_index, src, length), last;

			if (first >= 0) {
				prepare_move_submodule(src, first,
						       submodule_gitfile + i);
				goto act_on_entry;
			} else if (index_range_of_same_dir(src, length,
							   &first, &last) < 1) {
				bad = _("source directory is empty");
				goto act_on_entry;
			}

			/* last - first >= 1 */
			modes[i] |= WORKING_DIRECTORY;

			ALLOC_GROW(src_dir, src_dir_nr + 1, src_dir_alloc);
			src_dir[src_dir_nr++] = src;

			n = argc + last - first;
			REALLOC_ARRAY(source, n);
			REALLOC_ARRAY(destination, n);
			REALLOC_ARRAY(modes, n);
			REALLOC_ARRAY(submodule_gitfile, n);

			dst = add_slash(dst);
			dst_len = strlen(dst);

			for (j = 0; j < last - first; j++) {
				const struct cache_entry *ce = the_index.cache[first + j];
				const char *path = ce->name;
				source[argc + j] = path;
				destination[argc + j] =
					prefix_path(dst, dst_len, path + length + 1);
				memset(modes + argc + j, 0, sizeof(enum update_mode));
				modes[argc + j] |= ce_skip_worktree(ce) ? SPARSE : INDEX;
				submodule_gitfile[argc + j] = NULL;
			}
			argc += last - first;
			goto act_on_entry;
		}
		if (!(ce = index_file_exists(&the_index, src, length, 0))) {
			bad = _("not under version control");
			goto act_on_entry;
		}
		if (ce_stage(ce)) {
			bad = _("conflicted");
			goto act_on_entry;
		}
		if (lstat(dst, &st) == 0 &&
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
			goto act_on_entry;
		}
		if (string_list_has_string(&src_for_dst, dst)) {
			bad = _("multiple sources for the same target");
			goto act_on_entry;
		}
		if (is_dir_sep(dst[strlen(dst) - 1])) {
			bad = _("destination directory does not exist");
			goto act_on_entry;
		}

		if (ignore_sparse &&
		    (dst_mode & (SKIP_WORKTREE_DIR | SPARSE)) &&
		    index_entry_exists(&the_index, dst, strlen(dst))) {
			bad = _("destination exists in the index");
			if (force) {
				if (verbose)
					warning(_("overwriting '%s'"), dst);
				bad = NULL;
			} else {
				goto act_on_entry;
			}
		}
		/*
		 * We check if the paths are in the sparse-checkout
		 * definition as a very final check, since that
		 * allows us to point the user to the --sparse
		 * option as a way to have a successful run.
		 */
		if (!ignore_sparse &&
		    !path_in_sparse_checkout(src, &the_index)) {
			string_list_append(&only_match_skip_worktree, src);
			skip_sparse = 1;
		}
		if (!ignore_sparse &&
		    !path_in_sparse_checkout(dst, &the_index)) {
			string_list_append(&only_match_skip_worktree, dst);
			skip_sparse = 1;
		}

		if (skip_sparse)
			goto remove_entry;

		string_list_insert(&src_for_dst, dst);

act_on_entry:
		if (!bad)
			continue;
		if (!ignore_errors)
			die(_("%s, source=%s, destination=%s"),
			     bad, src, dst);
remove_entry:
		if (--argc > 0) {
			int n = argc - i;
			MOVE_ARRAY(source + i, source + i + 1, n);
			MOVE_ARRAY(destination + i, destination + i + 1, n);
			MOVE_ARRAY(modes + i, modes + i + 1, n);
			MOVE_ARRAY(submodule_gitfile + i,
				   submodule_gitfile + i + 1, n);
			i--;
		}
	}

	if (only_match_skip_worktree.nr) {
		advise_on_updating_sparse_paths(&only_match_skip_worktree);
		if (!ignore_errors)
			return 1;
	}

	for (i = 0; i < argc; i++) {
		const char *src = source[i], *dst = destination[i];
		enum update_mode mode = modes[i];
		int pos;
		int sparse_and_dirty = 0;
		struct checkout state = CHECKOUT_INIT;
		state.istate = &the_index;

		if (force)
			state.force = 1;
		if (show_only || verbose)
			printf(_("Renaming %s to %s\n"), src, dst);
		if (show_only)
			continue;
		if (!(mode & (INDEX | SPARSE | SKIP_WORKTREE_DIR)) &&
		    !(dst_mode & (SKIP_WORKTREE_DIR | SPARSE)) &&
		    rename(src, dst) < 0) {
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

		if (mode & (WORKING_DIRECTORY | SKIP_WORKTREE_DIR))
			continue;

		pos = index_name_pos(&the_index, src, strlen(src));
		assert(pos >= 0);
		if (!(mode & SPARSE) && !lstat(src, &st))
			sparse_and_dirty = ie_modified(&the_index,
						       the_index.cache[pos],
						       &st,
						       0);
		rename_index_entry_at(&the_index, pos, dst);

		if (ignore_sparse &&
		    core_apply_sparse_checkout &&
		    core_sparse_checkout_cone) {
			/*
			 * NEEDSWORK: we are *not* paying attention to
			 * "out-to-out" move (<source> is out-of-cone and
			 * <destination> is out-of-cone) at this point. It
			 * should be added in a future patch.
			 */
			if ((mode & SPARSE) &&
			    path_in_sparse_checkout(dst, &the_index)) {
				/* from out-of-cone to in-cone */
				int dst_pos = index_name_pos(&the_index, dst,
							     strlen(dst));
				struct cache_entry *dst_ce = the_index.cache[dst_pos];

				dst_ce->ce_flags &= ~CE_SKIP_WORKTREE;

				if (checkout_entry(dst_ce, &state, NULL, NULL))
					die(_("cannot checkout %s"), dst_ce->name);
			} else if ((dst_mode & (SKIP_WORKTREE_DIR | SPARSE)) &&
				   !(mode & SPARSE) &&
				   !path_in_sparse_checkout(dst, &the_index)) {
				/* from in-cone to out-of-cone */
				int dst_pos = index_name_pos(&the_index, dst,
							     strlen(dst));
				struct cache_entry *dst_ce = the_index.cache[dst_pos];

				/*
				 * if src is clean, it will suffice to remove it
				 */
				if (!sparse_and_dirty) {
					dst_ce->ce_flags |= CE_SKIP_WORKTREE;
					unlink_or_warn(src);
				} else {
					/*
					 * if src is dirty, move it to the
					 * destination and create leading
					 * dirs if necessary
					 */
					char *dst_dup = xstrdup(dst);
					string_list_append(&dirty_paths, dst);
					safe_create_leading_directories(dst_dup);
					FREE_AND_NULL(dst_dup);
					rename(src, dst);
				}
			}
		}
	}

	/*
	 * cleanup the empty src_dirs
	 */
	for (i = 0; i < src_dir_nr; i++) {
		int dummy;
		strbuf_addstr(&a_src_dir, src_dir[i]);
		/*
		 * if entries under a_src_dir are all moved away,
		 * recursively remove a_src_dir to cleanup
		 */
		if (index_range_of_same_dir(a_src_dir.buf, a_src_dir.len,
					    &dummy, &dummy) < 1) {
			remove_dir_recursively(&a_src_dir, 0);
		}
		strbuf_reset(&a_src_dir);
	}

	strbuf_release(&a_src_dir);
	free(src_dir);

	if (dirty_paths.nr)
		advise_on_moving_dirty_path(&dirty_paths);

	if (gitmodules_modified)
		stage_updated_gitmodules(&the_index);

	if (write_locked_index(&the_index, &lock_file,
			       COMMIT_LOCK | SKIP_IF_UNCHANGED))
		die(_("Unable to write new index file"));

	string_list_clear(&src_for_dst, 0);
	string_list_clear(&dirty_paths, 0);
	UNLEAK(source);
	UNLEAK(dest_path);
	free(submodule_gitfile);
	free(modes);
	return 0;
}
