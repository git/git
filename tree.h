#ifndef TREE_H
#define TREE_H

#include "object.h"

struct repository;
struct strbuf;

struct tree {
	struct object object;
	void *buffer;
	unsigned long size;
};

extern const char *tree_type;

struct tree *lookup_tree(struct repository *r, const struct object_id *oid);

int parse_tree_buffer(struct tree *item, void *buffer, unsigned long size);

int repo_parse_tree_gently(struct repository *r, struct tree *tree, int quiet_on_missing);
static inline int repo_parse_tree(struct repository *r, struct tree *tree)
{
	return repo_parse_tree_gently(r, tree, 0);
}

#ifndef NO_THE_REPOSITORY_COMPATIBILITY_MACROS
#define parse_tree(tree) repo_parse_tree(the_repository, tree)
#define parse_tree_gently(tree, quiet_on_missing) repo_parse_tree_gently(the_repository, tree, quiet_on_missing)
#define parse_tree_indirect(oid) repo_parse_tree_indirect(the_repository, oid)
#endif
void free_tree_buffer(struct tree *tree);

/* Parses and returns the tree in the given ent, chasing tags and commits. */
struct tree *repo_parse_tree_indirect(struct repository *r, const struct object_id *oid);

int cmp_cache_name_compare(const void *a_, const void *b_);

#define READ_TREE_RECURSIVE 1
typedef int (*read_tree_fn_t)(struct repository *r, const struct object_id *, struct strbuf *, const char *, unsigned int, void *);

int read_tree_at(struct repository *r,
		 struct tree *tree, struct strbuf *base,
		 const struct pathspec *pathspec,
		 read_tree_fn_t fn, void *context);

int read_tree(struct repository *r,
	      struct tree *tree,
	      const struct pathspec *pathspec,
	      read_tree_fn_t fn, void *context);

#endif /* TREE_H */
