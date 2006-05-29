#ifndef TREE_H
#define TREE_H

#include "object.h"

extern const char *tree_type;

struct tree_entry_list {
	struct tree_entry_list *next;
	unsigned directory : 1;
	unsigned executable : 1;
	unsigned symlink : 1;
	unsigned int mode;
	const char *name;
	const unsigned char *sha1;
};

struct tree {
	struct object object;
	void *buffer;
	unsigned long size;
};

struct tree_entry_list *create_tree_entry_list(struct tree *);
void free_tree_entry_list(struct tree_entry_list *);

struct tree *lookup_tree(const unsigned char *sha1);

int parse_tree_buffer(struct tree *item, void *buffer, unsigned long size);

int parse_tree(struct tree *tree);

/* Parses and returns the tree in the given ent, chasing tags and commits. */
struct tree *parse_tree_indirect(const unsigned char *sha1);

#define READ_TREE_RECURSIVE 1
typedef int (*read_tree_fn_t)(const unsigned char *, const char *, int, const char *, unsigned int, int);

extern int read_tree_recursive(struct tree *tree,
			       const char *base, int baselen,
			       int stage, const char **match,
			       read_tree_fn_t fn);

extern int read_tree(struct tree *tree, int stage, const char **paths);

#endif /* TREE_H */
