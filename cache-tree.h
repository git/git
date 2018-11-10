#ifndef CACHE_TREE_H
#define CACHE_TREE_H

#include "cache.h"
#include "tree.h"
#include "tree-walk.h"

struct cache_tree;
struct cache_tree_sub {
	struct cache_tree *cache_tree;
	int count;		/* internally used by update_one() */
	int namelen;
	int used;
	char name[FLEX_ARRAY];
};

struct cache_tree {
	int entry_count; /* negative means "invalid" */
	struct object_id oid;
	int subtree_nr;
	int subtree_alloc;
	struct cache_tree_sub **down;
};

struct cache_tree *cache_tree(void);
void cache_tree_free(struct cache_tree **);
void cache_tree_invalidate_path(struct index_state *, const char *);
struct cache_tree_sub *cache_tree_sub(struct cache_tree *, const char *);

void cache_tree_write(struct strbuf *, struct cache_tree *root);
struct cache_tree *cache_tree_read(const char *buffer, unsigned long size);

int cache_tree_fully_valid(struct cache_tree *);
int cache_tree_update(struct index_state *, int);
void cache_tree_verify(struct repository *, struct index_state *);

/* bitmasks to write_cache_as_tree flags */
#define WRITE_TREE_MISSING_OK 1
#define WRITE_TREE_IGNORE_CACHE_TREE 2
#define WRITE_TREE_DRY_RUN 4
#define WRITE_TREE_SILENT 8
#define WRITE_TREE_REPAIR 16

/* error return codes */
#define WRITE_TREE_UNREADABLE_INDEX (-1)
#define WRITE_TREE_UNMERGED_INDEX (-2)
#define WRITE_TREE_PREFIX_ERROR (-3)

int write_index_as_tree(struct object_id *oid, struct index_state *index_state, const char *index_path, int flags, const char *prefix);
void prime_cache_tree(struct repository *, struct index_state *, struct tree *);

int cache_tree_matches_traversal(struct cache_tree *, struct name_entry *ent, struct traverse_info *info);

#ifndef NO_THE_INDEX_COMPATIBILITY_MACROS
static inline int write_cache_as_tree(struct object_id *oid, int flags, const char *prefix)
{
	return write_index_as_tree(oid, &the_index, get_index_file(), flags, prefix);
}

static inline int update_main_cache_tree(int flags)
{
	if (!the_index.cache_tree)
		the_index.cache_tree = cache_tree();
	return cache_tree_update(&the_index, flags);
}
#endif

#endif
