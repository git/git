#ifndef TREE_H
#define TREE_H

#include "object.h"

struct pathspec;
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

int parse_tree_gently(struct tree *tree, int quiet_on_missing);
static inline int parse_tree(struct tree *tree)
{
	return parse_tree_gently(tree, 0);
}
void free_tree_buffer(struct tree *tree);

/* Parses and returns the tree in the given ent, chasing tags and commits. */
struct tree *parse_tree_indirect(const struct object_id *oid);

/*
 * Functions for comparing pathnames
 */
int base_name_compare(const char *name1, size_t len1, int mode1,
		      const char *name2, size_t len2, int mode2);
int df_name_compare(const char *name1, size_t len1, int mode1,
		    const char *name2, size_t len2, int mode2);
int name_compare(const char *name1, size_t len1, const char *name2,
		 size_t len2);

#define READ_TREE_RECURSIVE 1
typedef int (*read_tree_fn_t)(const struct object_id *, struct strbuf *,
			      const char *, unsigned int, void *);

int read_tree_at(struct repository *r, struct tree *tree, struct strbuf *base,
		 int depth, const struct pathspec *pathspec, read_tree_fn_t fn,
		 void *context);

int read_tree(struct repository *r, struct tree *tree,
	      const struct pathspec *pathspec, read_tree_fn_t fn,
	      void *context);

#endif /* TREE_H */
