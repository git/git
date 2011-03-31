#ifndef TREE_H
#define TREE_H

#include "object.h"

extern const char *tree_type;

struct tree {
	struct object object;
	void *buffer;
	unsigned long size;
};

struct tree *lookup_tree(const unsigned char *sha1);

int parse_tree_buffer(struct tree *item, void *buffer, unsigned long size);

int parse_tree(struct tree *tree);

/* Parses and returns the tree in the given ent, chasing tags and commits. */
struct tree *parse_tree_indirect(const unsigned char *sha1);

#define READ_TREE_RECURSIVE 1
typedef int (*read_tree_fn_t)(const unsigned char *, const char *, int, const char *, unsigned int, int, void *);

extern int read_tree_recursive(struct tree *tree,
			       const char *base, int baselen,
			       int stage, struct pathspec *pathspec,
			       read_tree_fn_t fn, void *context);

extern int read_tree(struct tree *tree, int stage, struct pathspec *pathspec);

#endif /* TREE_H */
