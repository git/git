/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */

#define USE_THE_INDEX_VARIABLE
#include "cache.h"
#include "config.h"
#include "lockfile.h"
#include "object.h"
#include "tree.h"
#include "tree-walk.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "dir.h"
#include "builtin.h"
#include "parse-options.h"
#include "resolve-undo.h"
#include "submodule.h"
#include "submodule-config.h"

static int nr_trees;
static int read_empty;
static struct tree *trees[MAX_UNPACK_TREES];

static int list_tree(struct object_id *oid)
{
	struct tree *tree;

	if (nr_trees >= MAX_UNPACK_TREES)
		die("I cannot read more than %d trees", MAX_UNPACK_TREES);
	tree = parse_tree_indirect(oid);
	if (!tree)
		return -1;
	trees[nr_trees++] = tree;
	return 0;
}

static const char * const read_tree_usage[] = {
	N_("git read-tree [(-m [--trivial] [--aggressive] | --reset | --prefix=<prefix>)\n"
	   "              [-u | -i]] [--index-output=<file>] [--no-sparse-checkout]\n"
	   "              (--empty | <tree-ish1> [<tree-ish2> [<tree-ish3>]])"),
	NULL
};

static int index_output_cb(const struct option *opt, const char *arg,
				 int unset)
{
	BUG_ON_OPT_NEG(unset);
	set_alternate_index_output(arg);
	return 0;
}

static int exclude_per_directory_cb(const struct option *opt, const char *arg,
				    int unset)
{
	struct unpack_trees_options *opts;

	BUG_ON_OPT_NEG(unset);

	opts = (struct unpack_trees_options *)opt->value;

	if (!opts->update)
		die("--exclude-per-directory is meaningless unless -u");
	if (strcmp(arg, ".gitignore"))
		die("--exclude-per-directory argument must be .gitignore");
	return 0;
}

static void debug_stage(const char *label, const struct cache_entry *ce,
			struct unpack_trees_options *o)
{
	printf("%s ", label);
	if (!ce)
		printf("(missing)\n");
	else if (ce == o->df_conflict_entry)
		printf("(conflict)\n");
	else
		printf("%06o #%d %s %.8s\n",
		       ce->ce_mode, ce_stage(ce), ce->name,
		       oid_to_hex(&ce->oid));
}

static int debug_merge(const struct cache_entry * const *stages,
		       struct unpack_trees_options *o)
{
	int i;

	printf("* %d-way merge\n", o->merge_size);
	debug_stage("index", stages[0], o);
	for (i = 1; i <= o->merge_size; i++) {
		char buf[24];
		xsnprintf(buf, sizeof(buf), "ent#%d", i);
		debug_stage(buf, stages[i], o);
	}
	return 0;
}

static int git_read_tree_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "submodule.recurse"))
		return git_default_submodule_config(var, value, cb);

	return git_default_config(var, value, cb);
}

int cmd_read_tree(int argc, const char **argv, const char *cmd_prefix)
{
	int i, stage = 0;
	struct object_id oid;
	struct tree_desc t[MAX_UNPACK_TREES];
	struct unpack_trees_options opts;
	int prefix_set = 0;
	struct lock_file lock_file = LOCK_INIT;
	const struct option read_tree_options[] = {
		OPT_CALLBACK_F(0, "index-output", NULL, N_("file"),
		  N_("write resulting index to <file>"),
		  PARSE_OPT_NONEG, index_output_cb),
		OPT_BOOL(0, "empty", &read_empty,
			    N_("only empty the index")),
		OPT__VERBOSE(&opts.verbose_update, N_("be verbose")),
		OPT_GROUP(N_("Merging")),
		OPT_BOOL('m', NULL, &opts.merge,
			 N_("perform a merge in addition to a read")),
		OPT_BOOL(0, "trivial", &opts.trivial_merges_only,
			 N_("3-way merge if no file level merging required")),
		OPT_BOOL(0, "aggressive", &opts.aggressive,
			 N_("3-way merge in presence of adds and removes")),
		OPT_BOOL(0, "reset", &opts.reset,
			 N_("same as -m, but discard unmerged entries")),
		{ OPTION_STRING, 0, "prefix", &opts.prefix, N_("<subdirectory>/"),
		  N_("read the tree into the index under <subdirectory>/"),
		  PARSE_OPT_NONEG },
		OPT_BOOL('u', NULL, &opts.update,
			 N_("update working tree with merge result")),
		OPT_CALLBACK_F(0, "exclude-per-directory", &opts,
		  N_("gitignore"),
		  N_("allow explicitly ignored files to be overwritten"),
		  PARSE_OPT_NONEG, exclude_per_directory_cb),
		OPT_BOOL('i', NULL, &opts.index_only,
			 N_("don't check the working tree after merging")),
		OPT__DRY_RUN(&opts.dry_run, N_("don't update the index or the work tree")),
		OPT_BOOL(0, "no-sparse-checkout", &opts.skip_sparse_checkout,
			 N_("skip applying sparse checkout filter")),
		OPT_BOOL(0, "debug-unpack", &opts.debug_unpack,
			 N_("debug unpack-trees")),
		OPT_CALLBACK_F(0, "recurse-submodules", NULL,
			    "checkout", "control recursive updating of submodules",
			    PARSE_OPT_OPTARG, option_parse_recurse_submodules_worktree_updater),
		OPT__QUIET(&opts.quiet, N_("suppress feedback messages")),
		OPT_END()
	};

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = -1;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;

	git_config(git_read_tree_config, NULL);

	argc = parse_options(argc, argv, cmd_prefix, read_tree_options,
			     read_tree_usage, 0);

	prefix_set = opts.prefix ? 1 : 0;
	if (1 < opts.merge + opts.reset + prefix_set)
		die("Which one? -m, --reset, or --prefix?");

	/* Prefix should not start with a directory separator */
	if (opts.prefix && opts.prefix[0] == '/')
		die("Invalid prefix, prefix cannot start with '/'");

	if (opts.reset)
		opts.reset = UNPACK_RESET_OVERWRITE_UNTRACKED;

	prepare_repo_settings(the_repository);
	the_repository->settings.command_requires_full_index = 0;

	repo_hold_locked_index(the_repository, &lock_file, LOCK_DIE_ON_ERROR);

	/*
	 * NEEDSWORK
	 *
	 * The old index should be read anyway even if we're going to
	 * destroy all index entries because we still need to preserve
	 * certain information such as index version or split-index
	 * mode.
	 */

	if (opts.reset || opts.merge || opts.prefix) {
		if (repo_read_index_unmerged(the_repository) && (opts.prefix || opts.merge))
			die(_("You need to resolve your current index first"));
		stage = opts.merge = 1;
	}
	resolve_undo_clear_index(&the_index);

	for (i = 0; i < argc; i++) {
		const char *arg = argv[i];

		if (get_oid(arg, &oid))
			die("Not a valid object name %s", arg);
		if (list_tree(&oid) < 0)
			die("failed to unpack tree object %s", arg);
		stage++;
	}
	if (!nr_trees && !read_empty && !opts.merge)
		warning("read-tree: emptying the index with no arguments is deprecated; use --empty");
	else if (nr_trees > 0 && read_empty)
		die("passing trees as arguments contradicts --empty");

	if (1 < opts.index_only + opts.update)
		die("-u and -i at the same time makes no sense");
	if ((opts.update || opts.index_only) && !opts.merge)
		die("%s is meaningless without -m, --reset, or --prefix",
		    opts.update ? "-u" : "-i");
	if (opts.update && !opts.reset)
		opts.preserve_ignored = 0;
	/* otherwise, opts.preserve_ignored is irrelevant */
	if (opts.merge && !opts.index_only)
		setup_work_tree();

	if (opts.skip_sparse_checkout)
		ensure_full_index(&the_index);

	if (opts.merge) {
		switch (stage - 1) {
		case 0:
			die("you must specify at least one tree to merge");
			break;
		case 1:
			opts.fn = opts.prefix ? bind_merge : oneway_merge;
			break;
		case 2:
			opts.fn = twoway_merge;
			opts.initial_checkout = is_index_unborn(&the_index);
			break;
		case 3:
		default:
			opts.fn = threeway_merge;
			break;
		}

		if (stage - 1 >= 3)
			opts.head_idx = stage - 2;
		else
			opts.head_idx = 1;
	}

	if (opts.debug_unpack)
		opts.fn = debug_merge;

	/* If we're going to prime_cache_tree later, skip cache tree update */
	if (nr_trees == 1 && !opts.prefix)
		opts.skip_cache_tree_update = 1;

	cache_tree_free(&the_index.cache_tree);
	for (i = 0; i < nr_trees; i++) {
		struct tree *tree = trees[i];
		parse_tree(tree);
		init_tree_desc(t+i, tree->buffer, tree->size);
	}
	if (unpack_trees(nr_trees, t, &opts))
		return 128;

	if (opts.debug_unpack || opts.dry_run)
		return 0; /* do not write the index out */

	/*
	 * When reading only one tree (either the most basic form,
	 * "-m ent" or "--reset ent" form), we can obtain a fully
	 * valid cache-tree because the index must match exactly
	 * what came from the tree.
	 */
	if (nr_trees == 1 && !opts.prefix)
		prime_cache_tree(the_repository,
				 the_repository->index,
				 trees[0]);

	if (write_locked_index(&the_index, &lock_file, COMMIT_LOCK))
		die("unable to write new index file");
	return 0;
}
