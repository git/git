#include "test-tool.h"
#include "cache.h"
#include "cummit-graph.h"
#include "cummit.h"
#include "config.h"
#include "object-store.h"
#include "object.h"
#include "repository.h"
#include "tree.h"

static void test_parse_cummit_in_graph(const char *butdir, const char *worktree,
				       const struct object_id *cummit_oid)
{
	struct repository r;
	struct cummit *c;
	struct cummit_list *parent;

	setup_but_env(butdir);

	memset(the_repository, 0, sizeof(*the_repository));

	if (repo_init(&r, butdir, worktree))
		die("Couldn't init repo");

	repo_set_hash_algo(the_repository, hash_algo_by_ptr(r.hash_algo));

	c = lookup_cummit(&r, cummit_oid);

	if (!parse_cummit_in_graph(&r, c))
		die("Couldn't parse cummit");

	printf("%"PRItime, c->date);
	for (parent = c->parents; parent; parent = parent->next)
		printf(" %s", oid_to_hex(&parent->item->object.oid));
	printf("\n");

	repo_clear(&r);
}

static void test_get_cummit_tree_in_graph(const char *butdir,
					  const char *worktree,
					  const struct object_id *cummit_oid)
{
	struct repository r;
	struct cummit *c;
	struct tree *tree;

	setup_but_env(butdir);

	memset(the_repository, 0, sizeof(*the_repository));

	if (repo_init(&r, butdir, worktree))
		die("Couldn't init repo");

	repo_set_hash_algo(the_repository, hash_algo_by_ptr(r.hash_algo));

	c = lookup_cummit(&r, cummit_oid);

	/*
	 * get_cummit_tree_in_graph does not automatically parse the cummit, so
	 * parse it first.
	 */
	if (!parse_cummit_in_graph(&r, c))
		die("Couldn't parse cummit");
	tree = get_cummit_tree_in_graph(&r, c);
	if (!tree)
		die("Couldn't get cummit tree");

	printf("%s\n", oid_to_hex(&tree->object.oid));

	repo_clear(&r);
}

int cmd__repository(int argc, const char **argv)
{
	int nonbut_ok = 0;

	setup_but_directory_gently(&nonbut_ok);

	if (argc < 2)
		die("must have at least 2 arguments");
	if (!strcmp(argv[1], "parse_cummit_in_graph")) {
		struct object_id oid;
		if (argc < 5)
			die("not enough arguments");
		if (parse_oid_hex(argv[4], &oid, &argv[4]))
			die("cannot parse oid '%s'", argv[4]);
		test_parse_cummit_in_graph(argv[2], argv[3], &oid);
	} else if (!strcmp(argv[1], "get_cummit_tree_in_graph")) {
		struct object_id oid;
		if (argc < 5)
			die("not enough arguments");
		if (parse_oid_hex(argv[4], &oid, &argv[4]))
			die("cannot parse oid '%s'", argv[4]);
		test_get_cummit_tree_in_graph(argv[2], argv[3], &oid);
	} else {
		die("unrecognized '%s'", argv[1]);
	}
	return 0;
}
