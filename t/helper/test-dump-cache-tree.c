#include "test-tool.h"
#include "hash.h"
#include "hex.h"
#include "tree.h"
#include "cache-tree.h"
#include "read-cache-ll.h"
#include "repository.h"
#include "setup.h"

static void dump_one(struct cache_tree *it, const char *pfx, const char *x)
{
	if (it->entry_count < 0)
		printf("%-40s %s%s (%d subtrees)\n",
		       "invalid", x, pfx, it->subtree_nr);
	else
		printf("%s %s%s (%d entries, %d subtrees)\n",
		       oid_to_hex(&it->oid), x, pfx,
		       it->entry_count, it->subtree_nr);
}

static int dump_cache_tree(struct cache_tree *it,
			   struct cache_tree *ref,
			   const char *pfx)
{
	int i;
	int errs = 0;

	if (!it || !ref)
		/* missing in either */
		return 0;

	if (it->entry_count < 0) {
		/* invalid */
		dump_one(it, pfx, "");
		dump_one(ref, pfx, "#(ref) ");
	}
	else {
		dump_one(it, pfx, "");
		if (!oideq(&it->oid, &ref->oid) ||
		    ref->entry_count != it->entry_count ||
		    ref->subtree_nr != it->subtree_nr) {
			/* claims to be valid but is lying */
			dump_one(ref, pfx, "#(ref) ");
			errs = 1;
		}
	}

	for (i = 0; i < it->subtree_nr; i++) {
		char path[PATH_MAX];
		struct cache_tree_sub *down = it->down[i];
		struct cache_tree_sub *rdwn;

		rdwn = cache_tree_sub(ref, down->name);
		xsnprintf(path, sizeof(path), "%s%.*s/", pfx, down->namelen, down->name);
		if (dump_cache_tree(down->cache_tree, rdwn->cache_tree, path))
			errs = 1;
	}
	return errs;
}

int cmd__dump_cache_tree(int ac UNUSED, const char **av UNUSED)
{
	struct index_state istate;
	struct cache_tree *another = cache_tree();
	int ret;

	setup_git_directory();
	if (repo_read_index(the_repository) < 0)
		die("unable to read index file");
	istate = *the_repository->index;
	istate.cache_tree = another;
	cache_tree_update(&istate, WRITE_TREE_DRY_RUN);
	ret = dump_cache_tree(the_repository->index->cache_tree, another, "");
	cache_tree_free(&another);

	return ret;
}
