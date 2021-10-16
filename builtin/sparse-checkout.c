#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "parse-options.h"
#include "pathspec.h"
#include "repository.h"
#include "run-command.h"
#include "strbuf.h"
#include "string-list.h"
#include "cache.h"
#include "cache-tree.h"
#include "lockfile.h"
#include "resolve-undo.h"
#include "unpack-trees.h"
#include "wt-status.h"
#include "quote.h"
#include "sparse-index.h"

static const char *empty_base = "";

static char const * const builtin_sparse_checkout_usage[] = {
	N_("git sparse-checkout (init|list|set|add|reapply|disable) <options>"),
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
	N_("git sparse-checkout list"),
	NULL
};

static int sparse_checkout_list(int argc, const char **argv)
{
	static struct option builtin_sparse_checkout_list_options[] = {
		OPT_END(),
	};
	struct pattern_list pl;
	char *sparse_filename;
	int res;

	argc = parse_options(argc, argv, NULL,
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

		return 0;
	}

	write_patterns_to_file(stdout, &pl);
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
	if (!r->index->sparse_index) {
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

	/* If no branch has been checked out, there are no updates to make. */
	if (is_index_unborn(r->index))
		return UPDATE_SPARSITY_SUCCESS;

	r->index->sparse_checkout_patterns = pl;

	memset(&o, 0, sizeof(o));
	o.verbose_update = isatty(2);
	o.update = 1;
	o.head_idx = -1;
	o.src_index = r->index;
	o.dst_index = r->index;
	o.skip_sparse_checkout = 0;
	o.pl = pl;

	setup_work_tree();

	repo_hold_locked_index(r, &lock_file, LOCK_DIE_ON_ERROR);

	setup_unpack_trees_porcelain(&o, "sparse-checkout");
	result = update_sparsity(&o);
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

	r->index->sparse_checkout_patterns = NULL;
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
}

static int write_patterns_and_update(struct pattern_list *pl)
{
	char *sparse_filename;
	FILE *fp;
	int fd;
	struct lock_file lk = LOCK_INIT;
	int result;

	sparse_filename = get_sparse_checkout_filename();

	if (safe_create_leading_directories(sparse_filename))
		die(_("failed to create directory for sparse-checkout file"));

	fd = hold_lock_file_for_update(&lk, sparse_filename,
				      LOCK_DIE_ON_ERROR);

	result = update_working_directory(pl);
	if (result) {
		rollback_lock_file(&lk);
		free(sparse_filename);
		clear_pattern_list(pl);
		update_working_directory(NULL);
		return result;
	}

	fp = xfdopen(fd, "w");

	if (core_sparse_checkout_cone)
		write_cone_to_file(fp, pl);
	else
		write_patterns_to_file(fp, pl);

	fflush(fp);
	commit_lock_file(&lk);

	free(sparse_filename);
	clear_pattern_list(pl);

	return 0;
}

enum sparse_checkout_mode {
	MODE_NO_PATTERNS = 0,
	MODE_ALL_PATTERNS = 1,
	MODE_CONE_PATTERNS = 2,
};

static int set_config(enum sparse_checkout_mode mode)
{
	const char *config_path;

	if (upgrade_repository_format(1) < 0)
		die(_("unable to upgrade repository format to enable worktreeConfig"));
	if (git_config_set_gently("extensions.worktreeConfig", "true")) {
		error(_("failed to set extensions.worktreeConfig setting"));
		return 1;
	}

	config_path = git_path("config.worktree");
	git_config_set_in_file_gently(config_path,
				      "core.sparseCheckout",
				      mode ? "true" : NULL);

	git_config_set_in_file_gently(config_path,
				      "core.sparseCheckoutCone",
				      mode == MODE_CONE_PATTERNS ? "true" : NULL);

	if (mode == MODE_NO_PATTERNS)
		set_sparse_index_config(the_repository, 0);

	return 0;
}

static char const * const builtin_sparse_checkout_init_usage[] = {
	N_("git sparse-checkout init [--cone] [--[no-]sparse-index]"),
	NULL
};

static struct sparse_checkout_init_opts {
	int cone_mode;
	int sparse_index;
} init_opts;

static int sparse_checkout_init(int argc, const char **argv)
{
	struct pattern_list pl;
	char *sparse_filename;
	int res;
	struct object_id oid;
	int mode;
	struct strbuf pattern = STRBUF_INIT;

	static struct option builtin_sparse_checkout_init_options[] = {
		OPT_BOOL(0, "cone", &init_opts.cone_mode,
			 N_("initialize the sparse-checkout in cone mode")),
		OPT_BOOL(0, "sparse-index", &init_opts.sparse_index,
			 N_("toggle the use of a sparse index")),
		OPT_END(),
	};

	repo_read_index(the_repository);

	init_opts.sparse_index = -1;

	argc = parse_options(argc, argv, NULL,
			     builtin_sparse_checkout_init_options,
			     builtin_sparse_checkout_init_usage, 0);

	if (init_opts.cone_mode) {
		mode = MODE_CONE_PATTERNS;
		core_sparse_checkout_cone = 1;
	} else
		mode = MODE_ALL_PATTERNS;

	if (set_config(mode))
		return 1;

	memset(&pl, 0, sizeof(pl));

	sparse_filename = get_sparse_checkout_filename();
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL, 0);

	if (init_opts.sparse_index >= 0) {
		if (set_sparse_index_config(the_repository, init_opts.sparse_index) < 0)
			die(_("failed to modify sparse-index config"));

		/* force an index rewrite */
		repo_read_index(the_repository);
		the_repository->index->updated_workdir = 1;
	}

	core_apply_sparse_checkout = 1;

	/* If we already have a sparse-checkout file, use it. */
	if (res >= 0) {
		free(sparse_filename);
		return update_working_directory(NULL);
	}

	if (get_oid("HEAD", &oid)) {
		FILE *fp;

		/* assume we are in a fresh repo, but update the sparse-checkout file */
		fp = xfopen(sparse_filename, "w");
		if (!fp)
			die(_("failed to open '%s'"), sparse_filename);

		free(sparse_filename);
		fprintf(fp, "/*\n!/*/\n");
		fclose(fp);
		return 0;
	}

	strbuf_addstr(&pattern, "/*");
	add_pattern(strbuf_detach(&pattern, NULL), empty_base, 0, &pl, 0);
	strbuf_addstr(&pattern, "!/*/");
	add_pattern(strbuf_detach(&pattern, NULL), empty_base, 0, &pl, 0);
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

		if (slash == e->pattern)
			break;

		newlen = slash - e->pattern;
		e = xmalloc(sizeof(struct pattern_entry));
		e->patternlen = newlen;
		e->pattern = xstrndup(oldpattern, newlen);
		hashmap_entry_init(&e->ent, fspathhash(e->pattern));

		if (!hashmap_get_entry(&pl->parent_hashmap, e, ent, NULL))
			hashmap_add(&pl->parent_hashmap, &e->ent);
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

static char const * const builtin_sparse_checkout_set_usage[] = {
	N_("git sparse-checkout (set|add) (--stdin | <patterns>)"),
	NULL
};

static struct sparse_checkout_set_opts {
	int use_stdin;
} set_opts;

static void add_patterns_from_input(struct pattern_list *pl,
				    int argc, const char **argv)
{
	int i;
	if (core_sparse_checkout_cone) {
		struct strbuf line = STRBUF_INIT;

		hashmap_init(&pl->recursive_hashmap, pl_hashmap_cmp, NULL, 0);
		hashmap_init(&pl->parent_hashmap, pl_hashmap_cmp, NULL, 0);
		pl->use_cone_patterns = 1;

		if (set_opts.use_stdin) {
			struct strbuf unquoted = STRBUF_INIT;
			while (!strbuf_getline(&line, stdin)) {
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
	} else {
		if (set_opts.use_stdin) {
			struct strbuf line = STRBUF_INIT;

			while (!strbuf_getline(&line, stdin)) {
				size_t len;
				char *buf = strbuf_detach(&line, &len);
				add_pattern(buf, empty_base, 0, pl, 0);
			}
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
				   struct pattern_list *pl)
{
	struct strbuf buffer = STRBUF_INIT;
	struct pattern_entry *pe;
	struct hashmap_iter iter;
	struct pattern_list existing;
	char *sparse_filename = get_sparse_checkout_filename();

	add_patterns_from_input(pl, argc, argv);

	memset(&existing, 0, sizeof(existing));
	existing.use_cone_patterns = core_sparse_checkout_cone;

	if (add_patterns_from_file_to_list(sparse_filename, "", 0,
					   &existing, NULL, 0))
		die(_("unable to load existing sparse-checkout patterns"));
	free(sparse_filename);

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
				 struct pattern_list *pl)
{
	char *sparse_filename = get_sparse_checkout_filename();
	if (add_patterns_from_file_to_list(sparse_filename, "", 0,
					   pl, NULL, 0))
		die(_("unable to load existing sparse-checkout patterns"));
	free(sparse_filename);
	add_patterns_from_input(pl, argc, argv);
}

static int modify_pattern_list(int argc, const char **argv, enum modify_type m)
{
	int result;
	int changed_config = 0;
	struct pattern_list *pl = xcalloc(1, sizeof(*pl));

	switch (m) {
	case ADD:
		if (core_sparse_checkout_cone)
			add_patterns_cone_mode(argc, argv, pl);
		else
			add_patterns_literal(argc, argv, pl);
		break;

	case REPLACE:
		add_patterns_from_input(pl, argc, argv);
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

static int sparse_checkout_set(int argc, const char **argv, const char *prefix,
			       enum modify_type m)
{
	static struct option builtin_sparse_checkout_set_options[] = {
		OPT_BOOL(0, "stdin", &set_opts.use_stdin,
			 N_("read patterns from standard in")),
		OPT_END(),
	};

	repo_read_index(the_repository);

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_set_options,
			     builtin_sparse_checkout_set_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	return modify_pattern_list(argc, argv, m);
}

static char const * const builtin_sparse_checkout_reapply_usage[] = {
	N_("git sparse-checkout reapply"),
	NULL
};

static int sparse_checkout_reapply(int argc, const char **argv)
{
	static struct option builtin_sparse_checkout_reapply_options[] = {
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL,
			     builtin_sparse_checkout_reapply_options,
			     builtin_sparse_checkout_reapply_usage, 0);

	repo_read_index(the_repository);
	return update_working_directory(NULL);
}

static char const * const builtin_sparse_checkout_disable_usage[] = {
	N_("git sparse-checkout disable"),
	NULL
};

static int sparse_checkout_disable(int argc, const char **argv)
{
	static struct option builtin_sparse_checkout_disable_options[] = {
		OPT_END(),
	};
	struct pattern_list pl;
	struct strbuf match_all = STRBUF_INIT;

	argc = parse_options(argc, argv, NULL,
			     builtin_sparse_checkout_disable_options,
			     builtin_sparse_checkout_disable_usage, 0);

	repo_read_index(the_repository);

	memset(&pl, 0, sizeof(pl));
	hashmap_init(&pl.recursive_hashmap, pl_hashmap_cmp, NULL, 0);
	hashmap_init(&pl.parent_hashmap, pl_hashmap_cmp, NULL, 0);
	pl.use_cone_patterns = 0;
	core_apply_sparse_checkout = 1;

	strbuf_addstr(&match_all, "/*");
	add_pattern(strbuf_detach(&match_all, NULL), empty_base, 0, &pl, 0);

	prepare_repo_settings(the_repository);
	the_repository->settings.sparse_index = 0;

	if (update_working_directory(&pl))
		die(_("error while refreshing working directory"));

	clear_pattern_list(&pl);
	return set_config(MODE_NO_PATTERNS);
}

int cmd_sparse_checkout(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_sparse_checkout_options[] = {
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_sparse_checkout_usage,
				   builtin_sparse_checkout_options);

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_options,
			     builtin_sparse_checkout_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	git_config(git_default_config, NULL);

	if (argc > 0) {
		if (!strcmp(argv[0], "list"))
			return sparse_checkout_list(argc, argv);
		if (!strcmp(argv[0], "init"))
			return sparse_checkout_init(argc, argv);
		if (!strcmp(argv[0], "set"))
			return sparse_checkout_set(argc, argv, prefix, REPLACE);
		if (!strcmp(argv[0], "add"))
			return sparse_checkout_set(argc, argv, prefix, ADD);
		if (!strcmp(argv[0], "reapply"))
			return sparse_checkout_reapply(argc, argv);
		if (!strcmp(argv[0], "disable"))
			return sparse_checkout_disable(argc, argv);
	}

	usage_with_options(builtin_sparse_checkout_usage,
			   builtin_sparse_checkout_options);
}
