#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "commit-graph.h"
#include "commit.h"
#include "environment.h"
#include "hex.h"
#include "object.h"
#include "repository.h"
#include "setup.h"
#include "tree.h"

static void test_parse_commit_in_graph(const char *gitdir, const char *worktree,
				       const struct object_id *commit_oid)
{
	struct repository r;
	struct commit *c;
	struct commit_list *parent;

	setup_git_env(gitdir);

	repo_clear(the_repository);

	if (repo_init(&r, gitdir, worktree))
		die("Couldn't init repo");

	repo_set_hash_algo(the_repository, hash_algo_by_ptr(r.hash_algo));

	c = lookup_commit(&r, commit_oid);

	if (!parse_commit_in_graph(&r, c))
		die("Couldn't parse commit");

	printf("%"PRItime, c->date);
	for (parent = c->parents; parent; parent = parent->next)
		printf(" %s", oid_to_hex(&parent->item->object.oid));
	printf("\n");

	repo_clear(&r);
}

static void test_get_commit_tree_in_graph(const char *gitdir,
					  const char *worktree,
					  const struct object_id *commit_oid)
{
	struct repository r;
	struct commit *c;
	struct tree *tree;

	setup_git_env(gitdir);

	repo_clear(the_repository);

	if (repo_init(&r, gitdir, worktree))
		die("Couldn't init repo");

	repo_set_hash_algo(the_repository, hash_algo_by_ptr(r.hash_algo));

	c = lookup_commit(&r, commit_oid);

	/*
	 * get_commit_tree_in_graph does not automatically parse the commit, so
	 * parse it first.
	 */
	if (!parse_commit_in_graph(&r, c))
		die("Couldn't parse commit");
	tree = get_commit_tree_in_graph(&r, c);
	if (!tree)
		die("Couldn't get commit tree");

	printf("%s\n", oid_to_hex(&tree->object.oid));

	repo_clear(&r);
}

int cmd__repository(int argc, const char **argv)
{
	int nongit_ok = 0;

	setup_git_directory_gently(&nongit_ok);

	if (argc < 2)
		die("must have at least 2 arguments");
	if (!strcmp(argv[1], "parse_commit_in_graph")) {
		struct object_id oid;
		if (argc < 5)
			die("not enough arguments");
		if (parse_oid_hex(argv[4], &oid, &argv[4]))
			die("cannot parse oid '%s'", argv[4]);
		test_parse_commit_in_graph(argv[2], argv[3], &oid);
	} else if (!strcmp(argv[1], "get_commit_tree_in_graph")) {
		struct object_id oid;
		if (argc < 5)
			die("not enough arguments");
		if (parse_oid_hex(argv[4], &oid, &argv[4]))
			die("cannot parse oid '%s'", argv[4]);
		test_get_commit_tree_in_graph(argv[2], argv[3], &oid);
	} else {
		die("unrecognized '%s'", argv[1]);
	}
	return 0;
}
