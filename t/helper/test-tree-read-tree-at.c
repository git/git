/* This tests tree.c's read_tree / read_tree_at.
We call it tree-read-tree-at to disambiguate with the read-tree tool.
*/
#include "cache.h"
#include "pathspec.h"
#include "test-tool.h"
#include "tree.h"

static int test_handle_entry(const struct object_id *oid,
		struct strbuf *base, const char *filename,
		unsigned mode, void *context UNUSED) {
	printf("%i %s %s%s\n", mode, oid_to_hex(oid), base->buf, filename);
	if (S_ISDIR(mode) || S_ISGITLINK(mode)) {
		return READ_TREE_RECURSIVE;
	}
	return 0;
}

int cmd__tree_read_tree_at(int argc UNUSED, const char **argv)
{
	struct pathspec pathspec;
	struct tree *tree;
	struct repository *repo;
	struct object_id oid;

	setup_git_directory();
	repo = the_repository;
	assert(repo);

	parse_pathspec(&pathspec, 0,
		       PATHSPEC_PREFER_FULL,
		       "", argv);

	assert(repo_get_oid(repo, "HEAD", &oid) == 0);
	tree = repo_parse_tree_indirect(repo, &oid);
	assert(tree);
	pathspec.recurse_submodules = 1;
	read_tree(repo, tree, &pathspec, test_handle_entry, NULL);
	return 0;
}
