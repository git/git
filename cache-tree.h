#ifndef CACHE_TREE_H
#define CACHE_TREE_H

struct cache_tree;
struct cache_tree_sub {
	struct cache_tree *cache_tree;
	int namelen;
	int used;
	char name[FLEX_ARRAY];
};

struct cache_tree {
	int entry_count; /* negative means "invalid" */
	unsigned char sha1[20];
	int subtree_nr;
	int subtree_alloc;
	struct cache_tree_sub **down;
};

struct cache_tree *cache_tree(void);
void cache_tree_free(struct cache_tree *);
void cache_tree_invalidate_path(struct cache_tree *, const char *);

int write_cache_tree(const unsigned char *, struct cache_tree *);
struct cache_tree *read_cache_tree(unsigned char *);
int cache_tree_update(struct cache_tree *, struct cache_entry **, int, int);


#endif
