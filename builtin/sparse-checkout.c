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

static const char *empty_base = "";

static char const * const builtin_sparse_checkout_usage[] = {
	N_("git sparse-checkout (init|list|set|disable) <options>"),
	NULL
};

static char *get_sparse_checkout_filename(void)
{
	return git_pathdup("info/sparse-checkout");
}

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

static int sparse_checkout_list(int argc, const char **argv)
{
	struct pattern_list pl;
	char *sparse_filename;
	int res;

	memset(&pl, 0, sizeof(pl));

	pl.use_cone_patterns = core_sparse_checkout_cone;

	sparse_filename = get_sparse_checkout_filename();
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL);
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

		for (i = 0; i < sl.nr; i++)
			printf("%s\n", sl.items[i].string);

		return 0;
	}

	write_patterns_to_file(stdout, &pl);
	clear_pattern_list(&pl);

	return 0;
}

static int update_working_directory(struct pattern_list *pl)
{
	int result = 0;
	struct unpack_trees_options o;
	struct lock_file lock_file = LOCK_INIT;
	struct object_id oid;
	struct tree *tree;
	struct tree_desc t;
	struct repository *r = the_repository;

	if (repo_read_index_unmerged(r))
		die(_("you need to resolve your current index first"));

	if (get_oid("HEAD", &oid))
		return 0;

	tree = parse_tree_indirect(&oid);
	parse_tree(tree);
	init_tree_desc(&t, tree->buffer, tree->size);

	memset(&o, 0, sizeof(o));
	o.verbose_update = isatty(2);
	o.merge = 1;
	o.update = 1;
	o.fn = oneway_merge;
	o.head_idx = -1;
	o.src_index = r->index;
	o.dst_index = r->index;
	o.skip_sparse_checkout = 0;
	o.pl = pl;
	o.keep_pattern_list = !!pl;

	resolve_undo_clear_index(r->index);
	setup_work_tree();

	cache_tree_free(&r->index->cache_tree);

	repo_hold_locked_index(r, &lock_file, LOCK_DIE_ON_ERROR);

	core_apply_sparse_checkout = 1;
	result = unpack_trees(1, &t, &o);

	if (!result) {
		prime_cache_tree(r, r->index, tree);
		write_locked_index(r->index, &lock_file, COMMIT_LOCK);
	} else
		rollback_lock_file(&lock_file);

	return result;
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
		char *pattern = sl.items[i].string;

		if (strlen(pattern))
			fprintf(fp, "%s/\n!%s/*/\n", pattern, pattern);
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
		char *pattern = sl.items[i].string;
		fprintf(fp, "%s/\n", pattern);
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

	return 0;
}

static char const * const builtin_sparse_checkout_init_usage[] = {
	N_("git sparse-checkout init [--cone]"),
	NULL
};

static struct sparse_checkout_init_opts {
	int cone_mode;
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
		OPT_END(),
	};

	repo_read_index(the_repository);
	require_clean_work_tree(the_repository,
				N_("initialize sparse-checkout"), NULL, 1, 0);

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
	res = add_patterns_from_file_to_list(sparse_filename, "", 0, &pl, NULL);

	/* If we already have a sparse-checkout file, use it. */
	if (res >= 0) {
		free(sparse_filename);
		core_apply_sparse_checkout = 1;
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

	return write_patterns_and_update(&pl);
}

static void insert_recursive_pattern(struct pattern_list *pl, struct strbuf *path)
{
	struct pattern_entry *e = xmalloc(sizeof(*e));
	e->patternlen = path->len;
	e->pattern = strbuf_detach(path, NULL);
	hashmap_entry_init(&e->ent,
			   ignore_case ?
			   strihash(e->pattern) :
			   strhash(e->pattern));

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
		hashmap_entry_init(&e->ent,
				   ignore_case ?
				   strihash(e->pattern) :
				   strhash(e->pattern));

		if (!hashmap_get_entry(&pl->parent_hashmap, e, ent, NULL))
			hashmap_add(&pl->parent_hashmap, &e->ent);
	}
}

static void strbuf_to_cone_pattern(struct strbuf *line, struct pattern_list *pl)
{
	strbuf_trim(line);

	strbuf_trim_trailing_dir_sep(line);

	if (!line->len)
		return;

	if (line->buf[0] != '/')
		strbuf_insert(line, 0, "/", 1);

	insert_recursive_pattern(pl, line);
}

static char const * const builtin_sparse_checkout_set_usage[] = {
	N_("git sparse-checkout set (--stdin | <patterns>)"),
	NULL
};

static struct sparse_checkout_set_opts {
	int use_stdin;
} set_opts;

static int sparse_checkout_set(int argc, const char **argv, const char *prefix)
{
	int i;
	struct pattern_list pl;
	int result;
	int changed_config = 0;

	static struct option builtin_sparse_checkout_set_options[] = {
		OPT_BOOL(0, "stdin", &set_opts.use_stdin,
			 N_("read patterns from standard in")),
		OPT_END(),
	};

	repo_read_index(the_repository);
	require_clean_work_tree(the_repository,
				N_("set sparse-checkout patterns"), NULL, 1, 0);

	memset(&pl, 0, sizeof(pl));

	argc = parse_options(argc, argv, prefix,
			     builtin_sparse_checkout_set_options,
			     builtin_sparse_checkout_set_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	if (core_sparse_checkout_cone) {
		struct strbuf line = STRBUF_INIT;

		hashmap_init(&pl.recursive_hashmap, pl_hashmap_cmp, NULL, 0);
		hashmap_init(&pl.parent_hashmap, pl_hashmap_cmp, NULL, 0);
		pl.use_cone_patterns = 1;

		if (set_opts.use_stdin) {
			while (!strbuf_getline(&line, stdin))
				strbuf_to_cone_pattern(&line, &pl);
		} else {
			for (i = 0; i < argc; i++) {
				strbuf_setlen(&line, 0);
				strbuf_addstr(&line, argv[i]);
				strbuf_to_cone_pattern(&line, &pl);
			}
		}
	} else {
		if (set_opts.use_stdin) {
			struct strbuf line = STRBUF_INIT;

			while (!strbuf_getline(&line, stdin)) {
				size_t len;
				char *buf = strbuf_detach(&line, &len);
				add_pattern(buf, empty_base, 0, &pl, 0);
			}
		} else {
			for (i = 0; i < argc; i++)
				add_pattern(argv[i], empty_base, 0, &pl, 0);
		}
	}

	if (!core_apply_sparse_checkout) {
		set_config(MODE_ALL_PATTERNS);
		core_apply_sparse_checkout = 1;
		changed_config = 1;
	}

	result = write_patterns_and_update(&pl);

	if (result && changed_config)
		set_config(MODE_NO_PATTERNS);

	clear_pattern_list(&pl);
	return result;
}

static int sparse_checkout_disable(int argc, const char **argv)
{
	struct pattern_list pl;
	struct strbuf match_all = STRBUF_INIT;

	repo_read_index(the_repository);
	require_clean_work_tree(the_repository,
				N_("disable sparse-checkout"), NULL, 1, 0);

	memset(&pl, 0, sizeof(pl));
	hashmap_init(&pl.recursive_hashmap, pl_hashmap_cmp, NULL, 0);
	hashmap_init(&pl.parent_hashmap, pl_hashmap_cmp, NULL, 0);
	pl.use_cone_patterns = 0;
	core_apply_sparse_checkout = 1;

	strbuf_addstr(&match_all, "/*");
	add_pattern(strbuf_detach(&match_all, NULL), empty_base, 0, &pl, 0);

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
			return sparse_checkout_set(argc, argv, prefix);
		if (!strcmp(argv[0], "disable"))
			return sparse_checkout_disable(argc, argv);
	}

	usage_with_options(builtin_sparse_checkout_usage,
			   builtin_sparse_checkout_options);
}
