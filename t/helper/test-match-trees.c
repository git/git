#include "test-tool.h"
#include "hex.h"
#include "match-trees.h"
#include "object-name.h"
#include "repository.h"
#include "setup.h"
#include "tree.h"

int cmd__match_trees(int ac UNUSED, const char **av)
{
	struct object_id hash1, hash2, shifted;
	struct tree *one, *two;

	setup_git_directory();

	if (repo_get_oid(the_repository, av[1], &hash1))
		die("cannot parse %s as an object name", av[1]);
	if (repo_get_oid(the_repository, av[2], &hash2))
		die("cannot parse %s as an object name", av[2]);
	one = parse_tree_indirect(&hash1);
	if (!one)
		die("not a tree-ish %s", av[1]);
	two = parse_tree_indirect(&hash2);
	if (!two)
		die("not a tree-ish %s", av[2]);

	shift_tree(the_repository, &one->object.oid, &two->object.oid, &shifted, -1);
	printf("shifted: %s\n", oid_to_hex(&shifted));

	return 0;
}
