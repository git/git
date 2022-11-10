#include "test-tool.h"
#include "cache.h"
#include "tree.h"
#include "cache-tree.h"
#include "parse-options.h"

static char const * const test_cache_tree_usage[] = {
	N_("test-tool cache-tree <options> (control|prime|update)"),
	NULL
};

int cmd__cache_tree(int argc, const char **argv)
{
	struct object_id oid;
	struct tree *tree;
	int empty = 0;
	int invalidate_qty = 0;
	int i;

	struct option options[] = {
		OPT_BOOL(0, "empty", &empty,
			 N_("clear the cache tree before each iteration")),
		OPT_INTEGER_F(0, "invalidate", &invalidate_qty,
			      N_("number of entries in the cache tree to invalidate (default 0)"),
			      PARSE_OPT_NONEG),
		OPT_END()
	};

	setup_git_directory();

	argc = parse_options(argc, argv, NULL, options, test_cache_tree_usage, 0);

	if (read_cache() < 0)
		die(_("unable to read index file"));

	oidcpy(&oid, &the_index.cache_tree->oid);
	tree = parse_tree_indirect(&oid);
	if (!tree)
		die(_("not a tree object: %s"), oid_to_hex(&oid));

	if (empty) {
		/* clear the cache tree & allocate a new one */
		cache_tree_free(&the_index.cache_tree);
		the_index.cache_tree = cache_tree();
	} else if (invalidate_qty) {
		/* invalidate the specified number of unique paths */
		float f_interval = (float)the_index.cache_nr / invalidate_qty;
		int interval = f_interval < 1.0 ? 1 : (int)f_interval;
		for (i = 0; i < invalidate_qty && i * interval < the_index.cache_nr; i++)
			cache_tree_invalidate_path(&the_index, the_index.cache[i * interval]->name);
	}

	if (argc != 1)
		usage_with_options(test_cache_tree_usage, options);
	else if (!strcmp(argv[0], "prime"))
		prime_cache_tree(the_repository, &the_index, tree);
	else if (!strcmp(argv[0], "update"))
		cache_tree_update(&the_index, WRITE_TREE_SILENT | WRITE_TREE_REPAIR);
	/* use "control" subcommand to specify no-op */
	else if (!!strcmp(argv[0], "control"))
		die(_("Unhandled subcommand '%s'"), argv[0]);

	return 0;
}
