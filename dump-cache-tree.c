#include "cache.h"
#include "tree.h"
#include "cache-tree.h"

static unsigned char active_cache_sha1[20];
static struct cache_tree *active_cache_tree;

static void dump_cache_tree(struct cache_tree *it, const char *pfx)
{
	int i;
	if (it->entry_count < 0)
		printf("%-40s %s\n", "invalid", pfx);
	else
		printf("%s %s (%d entries)\n",
		       sha1_to_hex(it->sha1),
		       pfx, it->entry_count);
	for (i = 0; i < it->subtree_nr; i++) {
		char path[PATH_MAX];
		struct cache_tree_sub *down = it->down[i];
		sprintf(path, "%s%.*s/", pfx, down->namelen, down->name);
		dump_cache_tree(down->cache_tree, path);
	}
}

int main(int ac, char **av)
{
	if (read_cache_1(active_cache_sha1) < 0)
		die("unable to read index file");
	active_cache_tree = read_cache_tree(active_cache_sha1);
	dump_cache_tree(active_cache_tree, "");
	return 0;
}
