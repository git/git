#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "environment.h"
#include "gettext.h"
#include "object-file.h"
#include "object-name.h"
#include "parse-options.h"
#include "pathspec.h"
#include "strbuf.h"
#include "string-list.h"
#include "lockfile.h"
#include "unpack-trees.h"
#include "quote.h"
#include "setup.h"
#include "sparse-index.h"
#include "worktree.h"

static const char *empty_base = "";

static char const * const builtin_sparse_checkout_usage[] = {
	N_("git sparse-checkout (init | list | set | add | reapply | disable | check-rules) [<options>]"),
	NULL
};

static void write_patterns_to_file(FILE *fp, struct pattern_list *pl)
{
	int i;

	for (i = 0; i < pl->nr; i++) {
		struct path_pattern *p = pl->patterns[i];

		if (p->flags & PATTERN_FLAG_NEGATIVE)
			fprintf(fp, "!");

		fprintf(fp, "%s", p->pattern);

		if (p->flags & PATTERN_FLAG_MUSTBEDIR)
			fprintf(fp, "/");

		fprintf(fp, "\n");
	}
}

static char const * const builtin_sparse_checkout_list_usage[] = {
	"git sparse-checkout list",
	NULL
};

static int sparse_checkout_list(int argc, const char **argv, const char *prefix,
				struct repository *repo UNUSED)
{
	static struct option builtin_sparse_checkout_list_options[] = {
		OPT_END(),
	};
	struct pattern_list pl;
	char *sparse_filename;
	int res;

	setup_work_tree();
	if (!core_apply_sparse_checkout)
		die(_("this worktree is not sparse"));

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_list_options,
			     builtin_sparse_checkout_list_usage, 0);

	memset(&pl, 0, sizeof(pl));

	pl.use_cone_patterns = core_sparse_checkout_cone;

	sparse_filename = get_sparse_checkout_filename();
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL, 0);
	free(sparse_filename);

	if (res < 0) {
		warning(_("this worktree is not sparse (sparse-checkout file may not exist)"));
		return 0;
	}

	if (pl.use_cone_patterns) {
		int i;
		struct pattern_entry *pe;
		struct hashmap_iter iter;
		struct string_list sl = STRING_LIST_INIT_DUP;

		hashmap_for_each_entry(&pl.recursive_hashmap, &iter, pe, ent) {
			/* pe->pattern starts with "/", skip it */
			string_list_insert(&sl, pe->pattern + 1);
		}

		string_list_sort(&sl);

		for (i = 0; i < sl.nr; i++) {
			quote_c_style(sl.items[i].string, NULL, stdout, 0);
			printf("\n");
		}

		string_list_clear(&sl, 0);
	} else {
		write_patterns_to_file(stdout, &pl);
	}

	clear_pattern_list(&pl);

	return 0;
}

static void clean_tracked_sparse_directories(struct repository *r)
{
	int i, was_full = 0;
	struct strbuf path = STRBUF_INIT;
	size_t pathlen;
	struct string_list_item *item;
	struct string_list sparse_dirs = STRING_LIST_INIT_DUP;

	/*
	 * If we are not using cone mode patterns, then we cannot
	 * delete directories outside of the sparse cone.
	 */
	if (!r || !r->index || !r->worktree)
		return;
	if (init_sparse_checkout_patterns(r->index) ||
	    !r->index->sparse_checkout_patterns->use_cone_patterns)
		return;

	/*
	 * Use the sparse index as a data structure to assist finding
	 * directories that are safe to delete. This conversion to a
	 * sparse index will not delete directories that contain
	 * conflicted entries or submodules.
	 */
	if (r->index->sparse_index == INDEX_EXPANDED) {
		/*
		 * If something, such as a merge conflict or other concern,
		 * prevents us from converting to a sparse index, then do
		 * not try deleting files.
		 */
		if (convert_to_sparse(r->index, SPARSE_INDEX_MEMORY_ONLY))
			return;
		was_full = 1;
	}

	strbuf_addstr(&path, r->worktree);
	strbuf_complete(&path, '/');
	pathlen = path.len;

	/*
	 * Collect directories that have gone out of scope but also
	 * exist on disk, so there is some work to be done. We need to
	 * store the entries in a list before exploring, since that might
	 * expand the sparse-index again.
	 */
	for (i = 0; i < r->index->cache_nr; i++) {
		struct cache_entry *ce = r->index->cache[i];

		if (S_ISSPARSEDIR(ce->ce_mode) &&
		    repo_file_exists(r, ce->name))
			string_list_append(&sparse_dirs, ce->name);
	}

	for_each_string_list_item(item, &sparse_dirs) {
		struct dir_struct dir = DIR_INIT;
		struct pathspec p = { 0 };
		struct strvec s = STRVEC_INIT;

		strbuf_setlen(&path, pathlen);
		strbuf_addstr(&path, item->string);

		dir.flags |= DIR_SHOW_IGNORED_TOO;

		setup_standard_excludes(&dir);
		strvec_push(&s, path.buf);

		parse_pathspec(&p, PATHSPEC_GLOB, 0, NULL, s.v);
		fill_directory(&dir, r->index, &p);

		if (dir.nr) {
			warning(_("directory '%s' contains untracked files,"
				  " but is not in the sparse-checkout cone"),
				item->string);
		} else if (remove_dir_recursively(&path, 0)) {
			/*
			 * Removal is "best effort". If something blocks
			 * the deletion, then continue with a warning.
			 */
			warning(_("failed to remove directory '%s'"),
				item->string);
		}

		strvec_clear(&s);
		clear_pathspec(&p);
		dir_clear(&dir);
	}

	string_list_clear(&sparse_dirs, 0);
	strbuf_release(&path);

	if (was_full)
		ensure_full_index(r->index);
}

static int update_working_directory(struct pattern_list *pl)
{
	enum update_sparsity_result result;
	struct unpack_trees_options o;
	struct lock_file lock_file = LOCK_INIT;
	struct repository *r = the_repository;
	struct pattern_list *old_pl;

	/* If no branch has been checked out, there are no updates to make. */
	if (is_index_unborn(r->index))
		return UPDATE_SPARSITY_SUCCESS;

	old_pl = r->index->sparse_checkout_patterns;
	r->index->sparse_checkout_patterns = pl;

	memset(&o, 0, sizeof(o));
	o.verbose_update = isatty(2);
	o.update = 1;
	o.head_idx = -1;
	o.src_index = r->index;
	o.dst_index = r->index;
	o.skip_sparse_checkout = 0;

	setup_work_tree();

	repo_hold_locked_index(r, &lock_file, LOCK_DIE_ON_ERROR);

	setup_unpack_trees_porcelain(&o, "sparse-checkout");
	result = update_sparsity(&o, pl);
	clear_unpack_trees_porcelain(&o);

	if (result == UPDATE_SPARSITY_WARNINGS)
		/*
		 * We don't do any special handling of warnings from untracked
		 * files in the way or dirty entries that can't be removed.
		 */
		result = UPDATE_SPARSITY_SUCCESS;
	if (result == UPDATE_SPARSITY_SUCCESS)
		write_locked_index(r->index, &lock_file, COMMIT_LOCK);
	else
		rollback_lock_file(&lock_file);

	clean_tracked_sparse_directories(r);

	if (r->index->sparse_checkout_patterns != pl) {
		clear_pattern_list(r->index->sparse_checkout_patterns);
		FREE_AND_NULL(r->index->sparse_checkout_patterns);
	}
	r->index->sparse_checkout_patterns = old_pl;

	return result;
}

static char *escaped_pattern(char *pattern)
{
	char *p = pattern;
	struct strbuf final = STRBUF_INIT;

	while (*p) {
		if (is_glob_special(*p))
			strbuf_addch(&final, '\\');

		strbuf_addch(&final, *p);
		p++;
	}

	return strbuf_detach(&final, NULL);
}

static void write_cone_to_file(FILE *fp, struct pattern_list *pl)
{
	int i;
	struct pattern_entry *pe;
	struct hashmap_iter iter;
	struct string_list sl = STRING_LIST_INIT_DUP;
	struct strbuf parent_pattern = STRBUF_INIT;

	hashmap_for_each_entry(&pl->parent_hashmap, &iter, pe, ent) {
		if (hashmap_get_entry(&pl->recursive_hashmap, pe, ent, NULL))
			continue;

		if (!hashmap_contains_parent(&pl->recursive_hashmap,
					     pe->pattern,
					     &parent_pattern))
			string_list_insert(&sl, pe->pattern);
	}

	string_list_sort(&sl);
	string_list_remove_duplicates(&sl, 0);

	fprintf(fp, "/*\n!/*/\n");

	for (i = 0; i < sl.nr; i++) {
		char *pattern = escaped_pattern(sl.items[i].string);

		if (strlen(pattern))
			fprintf(fp, "%s/\n!%s/*/\n", pattern, pattern);
		free(pattern);
	}

	string_list_clear(&sl, 0);

	hashmap_for_each_entry(&pl->recursive_hashmap, &iter, pe, ent) {
		if (!hashmap_contains_parent(&pl->recursive_hashmap,
					     pe->pattern,
					     &parent_pattern))
			string_list_insert(&sl, pe->pattern);
	}

	strbuf_release(&parent_pattern);

	string_list_sort(&sl);
	string_list_remove_duplicates(&sl, 0);

	for (i = 0; i < sl.nr; i++) {
		char *pattern = escaped_pattern(sl.items[i].string);
		fprintf(fp, "%s/\n", pattern);
		free(pattern);
	}

	string_list_clear(&sl, 0);
}

static int write_patterns_and_update(struct pattern_list *pl)
{
	char *sparse_filename;
	FILE *fp;
	struct lock_file lk = LOCK_INIT;
	int result;

	sparse_filename = get_sparse_checkout_filename();

	if (safe_create_leading_directories(sparse_filename))
		die(_("failed to create directory for sparse-checkout file"));

	hold_lock_file_for_update(&lk, sparse_filename, LOCK_DIE_ON_ERROR);

	result = update_working_directory(pl);
	if (result) {
		rollback_lock_file(&lk);
		update_working_directory(NULL);
		goto out;
	}

	fp = fdopen_lock_file(&lk, "w");
	if (!fp)
		die_errno(_("unable to fdopen %s"), get_lock_file_path(&lk));

	if (core_sparse_checkout_cone)
		write_cone_to_file(fp, pl);
	else
		write_patterns_to_file(fp, pl);

	if (commit_lock_file(&lk))
		die_errno(_("unable to write %s"), sparse_filename);

out:
	clear_pattern_list(pl);
	free(sparse_filename);
	return result;
}

enum sparse_checkout_mode {
	MODE_NO_PATTERNS = 0,
	MODE_ALL_PATTERNS = 1,
	MODE_CONE_PATTERNS = 2,
};

static int set_config(enum sparse_checkout_mode mode)
{
	/* Update to use worktree config, if not already. */
	if (init_worktree_config(the_repository)) {
		error(_("failed to initialize worktree config"));
		return 1;
	}

	if (repo_config_set_worktree_gently(the_repository,
					    "core.sparseCheckout",
					    mode ? "true" : "false") ||
	    repo_config_set_worktree_gently(the_repository,
					    "core.sparseCheckoutCone",
					    mode == MODE_CONE_PATTERNS ?
						"true" : "false"))
		return 1;

	if (mode == MODE_NO_PATTERNS)
		return set_sparse_index_config(the_repository, 0);

	return 0;
}

static enum sparse_checkout_mode update_cone_mode(int *cone_mode) {
	/* If not specified, use previous definition of cone mode */
	if (*cone_mode == -1 && core_apply_sparse_checkout)
		*cone_mode = core_sparse_checkout_cone;

	/* Set cone/non-cone mode appropriately */
	core_apply_sparse_checkout = 1;
	if (*cone_mode == 1 || *cone_mode == -1) {
		core_sparse_checkout_cone = 1;
		return MODE_CONE_PATTERNS;
	}
	core_sparse_checkout_cone = 0;
	return MODE_ALL_PATTERNS;
}

static int update_modes(int *cone_mode, int *sparse_index)
{
	int mode, record_mode;

	/* Determine if we need to record the mode; ensure sparse checkout on */
	record_mode = (*cone_mode != -1) || !core_apply_sparse_checkout;

	mode = update_cone_mode(cone_mode);
	if (record_mode && set_config(mode))
		return 1;

	/* Set sparse-index/non-sparse-index mode if specified */
	if (*sparse_index >= 0) {
		if (set_sparse_index_config(the_repository, *sparse_index) < 0)
			die(_("failed to modify sparse-index config"));

		/* force an index rewrite */
		repo_read_index(the_repository);
		the_repository->index->updated_workdir = 1;

		if (!*sparse_index)
			ensure_full_index(the_repository->index);
	}

	return 0;
}

static char const * const builtin_sparse_checkout_init_usage[] = {
	"git sparse-checkout init [--cone] [--[no-]sparse-index]",
	NULL
};

static struct sparse_checkout_init_opts {
	int cone_mode;
	int sparse_index;
} init_opts;

static int sparse_checkout_init(int argc, const char **argv, const char *prefix,
				struct repository *repo UNUSED)
{
	struct pattern_list pl;
	char *sparse_filename;
	int res;
	struct object_id oid;

	static struct option builtin_sparse_checkout_init_options[] = {
		OPT_BOOL(0, "cone", &init_opts.cone_mode,
			 N_("initialize the sparse-checkout in cone mode")),
		OPT_BOOL(0, "sparse-index", &init_opts.sparse_index,
			 N_("toggle the use of a sparse index")),
		OPT_END(),
	};

	setup_work_tree();
	repo_read_index(the_repository);

	init_opts.cone_mode = -1;
	init_opts.sparse_index = -1;

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_init_options,
			     builtin_sparse_checkout_init_usage, 0);

	if (update_modes(&init_opts.cone_mode, &init_opts.sparse_index))
		return 1;

	memset(&pl, 0, sizeof(pl));

	sparse_filename = get_sparse_checkout_filename();
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL, 0);

	/* If we already have a sparse-checkout file, use it. */
	if (res >= 0) {
		free(sparse_filename);
		clear_pattern_list(&pl);
		return update_working_directory(NULL);
	}

	if (repo_get_oid(the_repository, "HEAD", &oid)) {
		FILE *fp;

		/* assume we are in a fresh repo, but update the sparse-checkout file */
		if (safe_create_leading_directories(sparse_filename))
			die(_("unable to create leading directories of %s"),
			    sparse_filename);
		fp = xfopen(sparse_filename, "w");
		if (!fp)
			die(_("failed to open '%s'"), sparse_filename);

		free(sparse_filename);
		fprintf(fp, "/*\n!/*/\n");
		fclose(fp);
		return 0;
	}

	free(sparse_filename);

	add_pattern("/*", empty_base, 0, &pl, 0);
	add_pattern("!/*/", empty_base, 0, &pl, 0);
	pl.use_cone_patterns = init_opts.cone_mode;

	return write_patterns_and_update(&pl);
}

static void insert_recursive_pattern(struct pattern_list *pl, struct strbuf *path)
{
	struct pattern_entry *e = xmalloc(sizeof(*e));
	e->patternlen = path->len;
	e->pattern = strbuf_detach(path, NULL);
	hashmap_entry_init(&e->ent, fspathhash(e->pattern));

	hashmap_add(&pl->recursive_hashmap, &e->ent);

	while (e->patternlen) {
		char *slash = strrchr(e->pattern, '/');
		char *oldpattern = e->pattern;
		size_t newlen;
		struct pattern_entry *dup;

		if (!slash || slash == e->pattern)
			break;

		newlen = slash - e->pattern;
		e = xmalloc(sizeof(struct pattern_entry));
		e->patternlen = newlen;
		e->pattern = xstrndup(oldpattern, newlen);
		hashmap_entry_init(&e->ent, fspathhash(e->pattern));

		dup = hashmap_get_entry(&pl->parent_hashmap, e, ent, NULL);
		if (!dup) {
			hashmap_add(&pl->parent_hashmap, &e->ent);
		} else {
			free(e->pattern);
			free(e);
			e = dup;
		}
	}
}

static void strbuf_to_cone_pattern(struct strbuf *line, struct pattern_list *pl)
{
	strbuf_trim(line);

	strbuf_trim_trailing_dir_sep(line);

	if (strbuf_normalize_path(line))
		die(_("could not normalize path %s"), line->buf);

	if (!line->len)
		return;

	if (line->buf[0] != '/')
		strbuf_insertstr(line, 0, "/");

	insert_recursive_pattern(pl, line);
}

static void add_patterns_from_input(struct pattern_list *pl,
				    int argc, const char **argv,
				    FILE *file)
{
	int i;
	if (core_sparse_checkout_cone) {
		struct strbuf line = STRBUF_INIT;

		hashmap_init(&pl->recursive_hashmap, pl_hashmap_cmp, NULL, 0);
		hashmap_init(&pl->parent_hashmap, pl_hashmap_cmp, NULL, 0);
		pl->use_cone_patterns = 1;

		if (file) {
			struct strbuf unquoted = STRBUF_INIT;
			while (!strbuf_getline(&line, file)) {
				if (line.buf[0] == '"') {
					strbuf_reset(&unquoted);
					if (unquote_c_style(&unquoted, line.buf, NULL))
						die(_("unable to unquote C-style string '%s'"),
						line.buf);

					strbuf_swap(&unquoted, &line);
				}

				strbuf_to_cone_pattern(&line, pl);
			}

			strbuf_release(&unquoted);
		} else {
			for (i = 0; i < argc; i++) {
				strbuf_setlen(&line, 0);
				strbuf_addstr(&line, argv[i]);
				strbuf_to_cone_pattern(&line, pl);
			}
		}
		strbuf_release(&line);
	} else {
		if (file) {
			struct strbuf line = STRBUF_INIT;

			while (!strbuf_getline(&line, file))
				add_pattern(line.buf, empty_base, 0, pl, 0);

			strbuf_release(&line);
		} else {
			for (i = 0; i < argc; i++)
				add_pattern(argv[i], empty_base, 0, pl, 0);
		}
	}
}

enum modify_type {
	REPLACE,
	ADD,
};

static void add_patterns_cone_mode(int argc, const char **argv,
				   struct pattern_list *pl,
				   int use_stdin)
{
	struct strbuf buffer = STRBUF_INIT;
	struct pattern_entry *pe;
	struct hashmap_iter iter;
	struct pattern_list existing;
	char *sparse_filename = get_sparse_checkout_filename();

	add_patterns_from_input(pl, argc, argv,
				use_stdin ? stdin : NULL);

	memset(&existing, 0, sizeof(existing));
	existing.use_cone_patterns = core_sparse_checkout_cone;

	if (add_patterns_from_file_to_list(sparse_filename, "", 0,
					   &existing, NULL, 0))
		die(_("unable to load existing sparse-checkout patterns"));
	free(sparse_filename);

	if (!existing.use_cone_patterns)
		die(_("existing sparse-checkout patterns do not use cone mode"));

	hashmap_for_each_entry(&existing.recursive_hashmap, &iter, pe, ent) {
		if (!hashmap_contains_parent(&pl->recursive_hashmap,
					pe->pattern, &buffer) ||
		    !hashmap_contains_parent(&pl->parent_hashmap,
					pe->pattern, &buffer)) {
			strbuf_reset(&buffer);
			strbuf_addstr(&buffer, pe->pattern);
			insert_recursive_pattern(pl, &buffer);
		}
	}

	clear_pattern_list(&existing);
	strbuf_release(&buffer);
}

static void add_patterns_literal(int argc, const char **argv,
				 struct pattern_list *pl,
				 int use_stdin)
{
	char *sparse_filename = get_sparse_checkout_filename();
	if (add_patterns_from_file_to_list(sparse_filename, "", 0,
					   pl, NULL, 0))
		die(_("unable to load existing sparse-checkout patterns"));
	free(sparse_filename);
	add_patterns_from_input(pl, argc, argv, use_stdin ? stdin : NULL);
}

static int modify_pattern_list(struct strvec *args, int use_stdin,
			       enum modify_type m)
{
	int result;
	int changed_config = 0;
	struct pattern_list *pl = xcalloc(1, sizeof(*pl));

	switch (m) {
	case ADD:
		if (core_sparse_checkout_cone)
			add_patterns_cone_mode(args->nr, args->v, pl, use_stdin);
		else
			add_patterns_literal(args->nr, args->v, pl, use_stdin);
		break;

	case REPLACE:
		add_patterns_from_input(pl, args->nr, args->v,
					use_stdin ? stdin : NULL);
		break;
	}

	if (!core_apply_sparse_checkout) {
		set_config(MODE_ALL_PATTERNS);
		core_apply_sparse_checkout = 1;
		changed_config = 1;
	}

	result = write_patterns_and_update(pl);

	if (result && changed_config)
		set_config(MODE_NO_PATTERNS);

	clear_pattern_list(pl);
	free(pl);
	return result;
}

static void sanitize_paths(struct strvec *args,
			   const char *prefix, int skip_checks)
{
	int i;

	if (!args->nr)
		return;

	if (prefix && *prefix && core_sparse_checkout_cone) {
		/*
		 * The args are not pathspecs, so unfortunately we
		 * cannot imitate how cmd_add() uses parse_pathspec().
		 */
		int prefix_len = strlen(prefix);

		for (i = 0; i < args->nr; i++) {
			char *prefixed_path = prefix_path(prefix, prefix_len, args->v[i]);
			strvec_replace(args, i, prefixed_path);
			free(prefixed_path);
		}
	}

	if (skip_checks)
		return;

	if (prefix && *prefix && !core_sparse_checkout_cone)
		die(_("please run from the toplevel directory in non-cone mode"));

	if (core_sparse_checkout_cone) {
		for (i = 0; i < args->nr; i++) {
			if (args->v[i][0] == '/')
				die(_("specify directories rather than patterns (no leading slash)"));
			if (args->v[i][0] == '!')
				die(_("specify directories rather than patterns.  If your directory starts with a '!', pass --skip-checks"));
			if (strpbrk(args->v[i], "*?[]"))
				die(_("specify directories rather than patterns.  If your directory really has any of '*?[]\\' in it, pass --skip-checks"));
		}
	}

	for (i = 0; i < args->nr; i++) {
		struct cache_entry *ce;
		struct index_state *index = the_repository->index;
		int pos = index_name_pos(index, args->v[i], strlen(args->v[i]));

		if (pos < 0)
			continue;
		ce = index->cache[pos];
		if (S_ISSPARSEDIR(ce->ce_mode))
			continue;

		if (core_sparse_checkout_cone)
			die(_("'%s' is not a directory; to treat it as a directory anyway, rerun with --skip-checks"), args->v[i]);
		else
			warning(_("pass a leading slash before paths such as '%s' if you want a single file (see NON-CONE PROBLEMS in the git-sparse-checkout manual)."), args->v[i]);
	}
}

static char const * const builtin_sparse_checkout_add_usage[] = {
	N_("git sparse-checkout add [--skip-checks] (--stdin | <patterns>)"),
	NULL
};

static struct sparse_checkout_add_opts {
	int skip_checks;
	int use_stdin;
} add_opts;

static int sparse_checkout_add(int argc, const char **argv, const char *prefix,
			       struct repository *repo UNUSED)
{
	static struct option builtin_sparse_checkout_add_options[] = {
		OPT_BOOL_F(0, "skip-checks", &add_opts.skip_checks,
			   N_("skip some sanity checks on the given paths that might give false positives"),
			   PARSE_OPT_NONEG),
		OPT_BOOL(0, "stdin", &add_opts.use_stdin,
			 N_("read patterns from standard in")),
		OPT_END(),
	};
	struct strvec patterns = STRVEC_INIT;
	int ret;

	setup_work_tree();
	if (!core_apply_sparse_checkout)
		die(_("no sparse-checkout to add to"));

	repo_read_index(the_repository);

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_add_options,
			     builtin_sparse_checkout_add_usage, 0);

	for (int i = 0; i < argc; i++)
		strvec_push(&patterns, argv[i]);
	sanitize_paths(&patterns, prefix, add_opts.skip_checks);

	ret = modify_pattern_list(&patterns, add_opts.use_stdin, ADD);

	strvec_clear(&patterns);
	return ret;
}

static char const * const builtin_sparse_checkout_set_usage[] = {
	N_("git sparse-checkout set [--[no-]cone] [--[no-]sparse-index] [--skip-checks] (--stdin | <patterns>)"),
	NULL
};

static struct sparse_checkout_set_opts {
	int cone_mode;
	int sparse_index;
	int skip_checks;
	int use_stdin;
} set_opts;

static int sparse_checkout_set(int argc, const char **argv, const char *prefix,
			       struct repository *repo UNUSED)
{
	int default_patterns_nr = 2;
	const char *default_patterns[] = {"/*", "!/*/", NULL};

	static struct option builtin_sparse_checkout_set_options[] = {
		OPT_BOOL(0, "cone", &set_opts.cone_mode,
			 N_("initialize the sparse-checkout in cone mode")),
		OPT_BOOL(0, "sparse-index", &set_opts.sparse_index,
			 N_("toggle the use of a sparse index")),
		OPT_BOOL_F(0, "skip-checks", &set_opts.skip_checks,
			   N_("skip some sanity checks on the given paths that might give false positives"),
			   PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "stdin", &set_opts.use_stdin,
			   N_("read patterns from standard in"),
			   PARSE_OPT_NONEG),
		OPT_END(),
	};
	struct strvec patterns = STRVEC_INIT;
	int ret;

	setup_work_tree();
	repo_read_index(the_repository);

	set_opts.cone_mode = -1;
	set_opts.sparse_index = -1;

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_set_options,
			     builtin_sparse_checkout_set_usage, 0);

	if (update_modes(&set_opts.cone_mode, &set_opts.sparse_index))
		return 1;

	/*
	 * Cone mode automatically specifies the toplevel directory.  For
	 * non-cone mode, if nothing is specified, manually select just the
	 * top-level directory (much as 'init' would do).
	 */
	if (!core_sparse_checkout_cone && !set_opts.use_stdin && argc == 0) {
		for (int i = 0; i < default_patterns_nr; i++)
			strvec_push(&patterns, default_patterns[i]);
	} else {
		for (int i = 0; i < argc; i++)
			strvec_push(&patterns, argv[i]);
		sanitize_paths(&patterns, prefix, set_opts.skip_checks);
	}

	ret = modify_pattern_list(&patterns, set_opts.use_stdin, REPLACE);

	strvec_clear(&patterns);
	return ret;
}

static char const * const builtin_sparse_checkout_reapply_usage[] = {
	"git sparse-checkout reapply [--[no-]cone] [--[no-]sparse-index]",
	NULL
};

static struct sparse_checkout_reapply_opts {
	int cone_mode;
	int sparse_index;
} reapply_opts;

static int sparse_checkout_reapply(int argc, const char **argv,
				   const char *prefix,
				   struct repository *repo UNUSED)
{
	static struct option builtin_sparse_checkout_reapply_options[] = {
		OPT_BOOL(0, "cone", &reapply_opts.cone_mode,
			 N_("initialize the sparse-checkout in cone mode")),
		OPT_BOOL(0, "sparse-index", &reapply_opts.sparse_index,
			 N_("toggle the use of a sparse index")),
		OPT_END(),
	};

	setup_work_tree();
	if (!core_apply_sparse_checkout)
		die(_("must be in a sparse-checkout to reapply sparsity patterns"));

	reapply_opts.cone_mode = -1;
	reapply_opts.sparse_index = -1;

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_reapply_options,
			     builtin_sparse_checkout_reapply_usage, 0);

	repo_read_index(the_repository);

	if (update_modes(&reapply_opts.cone_mode, &reapply_opts.sparse_index))
		return 1;

	return update_working_directory(NULL);
}

static char const * const builtin_sparse_checkout_disable_usage[] = {
	"git sparse-checkout disable",
	NULL
};

static int sparse_checkout_disable(int argc, const char **argv,
				   const char *prefix,
				   struct repository *repo UNUSED)
{
	static struct option builtin_sparse_checkout_disable_options[] = {
		OPT_END(),
	};
	struct pattern_list pl;

	/*
	 * We do not exit early if !core_apply_sparse_checkout; due to the
	 * ability for users to manually muck things up between
	 *   direct editing of .git/info/sparse-checkout
	 *   running read-tree -m u HEAD or update-index --skip-worktree
	 *   direct toggling of config options
	 * users might end up with an index with SKIP_WORKTREE bit set on
	 * some files and not know how to undo it.  So, here we just
	 * forcibly return to a dense checkout regardless of initial state.
	 */

	setup_work_tree();
	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_disable_options,
			     builtin_sparse_checkout_disable_usage, 0);

	/*
	 * Disable the advice message for expanding a sparse index, as we
	 * are expecting to do that when disabling sparse-checkout.
	 */
	give_advice_on_expansion = 0;
	repo_read_index(the_repository);

	memset(&pl, 0, sizeof(pl));
	hashmap_init(&pl.recursive_hashmap, pl_hashmap_cmp, NULL, 0);
	hashmap_init(&pl.parent_hashmap, pl_hashmap_cmp, NULL, 0);
	pl.use_cone_patterns = 0;
	core_apply_sparse_checkout = 1;

	add_pattern("/*", empty_base, 0, &pl, 0);

	prepare_repo_settings(the_repository);
	the_repository->settings.sparse_index = 0;

	if (update_working_directory(&pl))
		die(_("error while refreshing working directory"));

	clear_pattern_list(&pl);
	return set_config(MODE_NO_PATTERNS);
}

static char const * const builtin_sparse_checkout_check_rules_usage[] = {
	N_("git sparse-checkout check-rules [-z] [--skip-checks]"
	   "[--[no-]cone] [--rules-file <file>]"),
	NULL
};

static struct sparse_checkout_check_rules_opts {
	int cone_mode;
	int null_termination;
	char *rules_file;
} check_rules_opts;

static int check_rules(struct pattern_list *pl, int null_terminated) {
	struct strbuf line = STRBUF_INIT;
	struct strbuf unquoted = STRBUF_INIT;
	char *path;
	int line_terminator = null_terminated ? 0 : '\n';
	strbuf_getline_fn getline_fn = null_terminated ? strbuf_getline_nul
		: strbuf_getline;
	the_repository->index->sparse_checkout_patterns = pl;
	while (!getline_fn(&line, stdin)) {
		path = line.buf;
		if (!null_terminated && line.buf[0] == '"') {
			strbuf_reset(&unquoted);
			if (unquote_c_style(&unquoted, line.buf, NULL))
				die(_("unable to unquote C-style string '%s'"),
					line.buf);

			path = unquoted.buf;
		}

		if (path_in_sparse_checkout(path, the_repository->index))
			write_name_quoted(path, stdout, line_terminator);
	}
	strbuf_release(&line);
	strbuf_release(&unquoted);

	return 0;
}

static int sparse_checkout_check_rules(int argc, const char **argv, const char *prefix,
				       struct repository *repo UNUSED)
{
	static struct option builtin_sparse_checkout_check_rules_options[] = {
		OPT_BOOL('z', NULL, &check_rules_opts.null_termination,
			 N_("terminate input and output files by a NUL character")),
		OPT_BOOL(0, "cone", &check_rules_opts.cone_mode,
			 N_("when used with --rules-file interpret patterns as cone mode patterns")),
		OPT_FILENAME(0, "rules-file", &check_rules_opts.rules_file,
			 N_("use patterns in <file> instead of the current ones.")),
		OPT_END(),
	};

	FILE *fp;
	int ret;
	struct pattern_list pl = {0};
	char *sparse_filename;
	check_rules_opts.cone_mode = -1;

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_check_rules_options,
			     builtin_sparse_checkout_check_rules_usage, 0);

	if (check_rules_opts.rules_file && check_rules_opts.cone_mode < 0)
		check_rules_opts.cone_mode = 1;

	update_cone_mode(&check_rules_opts.cone_mode);
	pl.use_cone_patterns = core_sparse_checkout_cone;
	if (check_rules_opts.rules_file) {
		fp = xfopen(check_rules_opts.rules_file, "r");
		add_patterns_from_input(&pl, argc, argv, fp);
		fclose(fp);
	} else {
		sparse_filename = get_sparse_checkout_filename();
		if (add_patterns_from_file_to_list(sparse_filename, "", 0, &pl,
						   NULL, 0))
			die(_("unable to load existing sparse-checkout patterns"));
		free(sparse_filename);
	}

	ret = check_rules(&pl, check_rules_opts.null_termination);
	clear_pattern_list(&pl);
	free(check_rules_opts.rules_file);
	return ret;
}

int cmd_sparse_checkout(int argc,
			const char **argv,
			const char *prefix,
			struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option builtin_sparse_checkout_options[] = {
		OPT_SUBCOMMAND("list", &fn, sparse_checkout_list),
		OPT_SUBCOMMAND("init", &fn, sparse_checkout_init),
		OPT_SUBCOMMAND("set", &fn, sparse_checkout_set),
		OPT_SUBCOMMAND("add", &fn, sparse_checkout_add),
		OPT_SUBCOMMAND("reapply", &fn, sparse_checkout_reapply),
		OPT_SUBCOMMAND("disable", &fn, sparse_checkout_disable),
		OPT_SUBCOMMAND("check-rules", &fn, sparse_checkout_check_rules),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_options,
			     builtin_sparse_checkout_usage, 0);

	git_config(git_default_config, NULL);

	prepare_repo_settings(the_repository);
	the_repository->settings.command_requires_full_index = 0;

	return fn(argc, argv, prefix, repo);
}
