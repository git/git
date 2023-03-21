#define USE_THE_INDEX_VARIABLE
#include "test-tool.h"
#include "cache.h"
#include "lockfile.h"
#include "setup.h"
#include "tree.h"
#include "cache-tree.h"

int cmd__scrap_cache_tree(int ac, const char **av)
{
	struct lock_file index_lock = LOCK_INIT;

	setup_git_directory();
	repo_hold_locked_index(the_repository, &index_lock, LOCK_DIE_ON_ERROR);
	if (repo_read_index(the_repository) < 0)
		die("unable to read index file");
	cache_tree_free(&the_index.cache_tree);
	the_index.cache_tree = NULL;
	if (write_locked_index(&the_index, &index_lock, COMMIT_LOCK))
		die("unable to write index file");
	return 0;
}
