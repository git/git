/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */

#include "cache.h"
#include "object.h"
#include "tree.h"
#include "tree-walk.h"
#include "cache-tree.h"
#include "unpack-trees.h"
#include "dir.h"
#include "builtin.h"
#include "parse-options.h"
#include "resolve-undo.h"

static int nr_trees;
static int read_empty;
static struct tree *trees[MAX_UNPACK_TREES];

static int list_tree(unsigned char *sha1)
{
	struct tree *tree;

	if (nr_trees >= MAX_UNPACK_TREES)
		die("I cannot read more than %d trees", MAX_UNPACK_TREES);
	tree = parse_tree_indirect(sha1);
	if (!tree)
		return -1;
	trees[nr_trees++] = tree;
	return 0;
}

static const char * const read_tree_usage[] = {
	"git read-tree [[-m [--trivial] [--aggressive] | --reset | --prefix=<prefix>] [-u [--exclude-per-directory=<gitignore>] | -i]] [--no-sparse-checkout] [--index-output=<file>] (--empty | <tree-ish1> [<tree-ish2> [<tree-ish3>]])",
	NULL
};

static int index_output_cb(const struct option *opt, const char *arg,
				 int unset)
{
	set_alternate_index_output(arg);
	return 0;
}

static int exclude_per_directory_cb(const struct option *opt, const char *arg,
				    int unset)
{
	struct dir_struct *dir;
	struct unpack_trees_options *opts;

	opts = (struct unpack_trees_options *)opt->value;

	if (opts->dir)
		die("more than one --exclude-per-directory given.");

	dir = xcalloc(1, sizeof(*opts->dir));
	dir->flags |= DIR_SHOW_IGNORED;
	dir->exclude_per_dir = arg;
	opts->dir = dir;
	/* We do not need to nor want to do read-directory
	 * here; we are merely interested in reusing the
	 * per directory ignore stack mechanism.
	 */
	return 0;
}

static void debug_stage(const char *label, struct cache_entry *ce,
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
		       sha1_to_hex(ce->sha1));
}

static int debug_merge(struct cache_entry **stages, struct unpack_trees_options *o)
{
	int i;

	printf("* %d-way merge\n", o->merge_size);
	debug_stage("index", stages[0], o);
	for (i = 1; i <= o->merge_size; i++) {
		char buf[24];
		sprintf(buf, "ent#%d", i);
		debug_stage(buf, stages[i], o);
	}
	return 0;
}

static struct lock_file lock_file;

int cmd_read_tree(int argc, const char **argv, const char *unused_prefix)
{
	int i, newfd, stage = 0;
	unsigned char sha1[20];
	struct tree_desc t[MAX_UNPACK_TREES];
	struct unpack_trees_options opts;
	int prefix_set = 0;
	const struct option read_tree_options[] = {
		{ OPTION_CALLBACK, 0, "index-output", NULL, "file",
		  "write resulting index to <file>",
		  PARSE_OPT_NONEG, index_output_cb },
		OPT_SET_INT(0, "empty", &read_empty,
			    "only empty the index", 1),
		OPT__VERBOSE(&opts.verbose_update, "be verbose"),
		OPT_GROUP("Merging"),
		OPT_SET_INT('m', NULL, &opts.merge,
			    "perform a merge in addition to a read", 1),
		OPT_SET_INT(0, "trivial", &opts.trivial_merges_only,
			    "3-way merge if no file level merging required", 1),
		OPT_SET_INT(0, "aggressive", &opts.aggressive,
			    "3-way merge in presence of adds and removes", 1),
		OPT_SET_INT(0, "reset", &opts.reset,
			    "same as -m, but discard unmerged entries", 1),
		{ OPTION_STRING, 0, "prefix", &opts.prefix, "<subdirectory>/",
		  "read the tree into the index under <subdirectory>/",
		  PARSE_OPT_NONEG | PARSE_OPT_LITERAL_ARGHELP },
		OPT_SET_INT('u', NULL, &opts.update,
			    "update working tree with merge result", 1),
		{ OPTION_CALLBACK, 0, "exclude-per-directory", &opts,
		  "gitignore",
		  "allow explicitly ignored files to be overwritten",
		  PARSE_OPT_NONEG, exclude_per_directory_cb },
		OPT_SET_INT('i', NULL, &opts.index_only,
			    "don't check the working tree after merging", 1),
		OPT__DRY_RUN(&opts.dry_run, "don't update the index or the work tree"),
		OPT_SET_INT(0, "no-sparse-checkout", &opts.skip_sparse_checkout,
			    "skip applying sparse checkout filter", 1),
		OPT_SET_INT(0, "debug-unpack", &opts.debug_unpack,
			    "debug unpack-trees", 1),
		OPT_END()
	};

	memset(&opts, 0, sizeof(opts));
	opts.head_idx = -1;
	opts.src_index = &the_index;
	opts.dst_index = &the_index;

	git_config(git_default_config, NULL);

	argc = parse_options(argc, argv, unused_prefix, read_tree_options,
			     read_tree_usage, 0);

	newfd = hold_locked_index(&lock_file, 1);

	prefix_set = opts.prefix ? 1 : 0;
	if (1 < opts.merge + opts.reset + prefix_set)
		die("Which one? -m, --reset, or --prefix?");

	if (opts.reset || opts.merge || opts.prefix) {
		if (read_cache_unmerged() && (opts.prefix || opts.merge))
			die("You need to resolve your current index first");
		stage = opts.merge = 1;
	}
	resolve_undo_clear();

	for (i = 0; i < argc; i++) {
		const char *arg = argv[i];

		if (get_sha1(arg, sha1))
			die("Not a valid object name %s", arg);
		if (list_tree(sha1) < 0)
			die("failed to unpack tree object %s", arg);
		stage++;
	}
	if (nr_trees == 0 && !read_empty)
		warning("read-tree: emptying the index with no arguments is deprecated; use --empty");
	else if (nr_trees > 0 && read_empty)
		die("passing trees as arguments contradicts --empty");

	if (1 < opts.index_only + opts.update)
		die("-u and -i at the same time makes no sense");
	if ((opts.update||opts.index_only) && !opts.merge)
		die("%s is meaningless without -m, --reset, or --prefix",
		    opts.update ? "-u" : "-i");
	if ((opts.dir && !opts.update))
		die("--exclude-per-directory is meaningless unless -u");
	if (opts.merge && !opts.index_only)
		setup_work_tree();

	if (opts.merge) {
		if (stage < 2)
			die("just how do you expect me to merge %d trees?", stage-1);
		switch (stage - 1) {
		case 1:
			opts.fn = opts.prefix ? bind_merge : oneway_merge;
			break;
		case 2:
			opts.fn = twoway_merge;
			opts.initial_checkout = is_cache_unborn();
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

	cache_tree_free(&active_cache_tree);
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
		prime_cache_tree(&active_cache_tree, trees[0]);

	if (write_cache(newfd, active_cache, active_nr) ||
	    commit_locked_index(&lock_file))
		die("unable to write new index file");
	return 0;
}
