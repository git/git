#ifndef TREE_H
#define TREE_H

#include "object.h"

extern const char *tree_type;

struct tree_entry_list {
	struct tree_entry_list *next;
	unsigned directory : 1;
	unsigned executable : 1;
	unsigned symlink : 1;
	unsigned zeropad : 1;
	unsigned int mode;
	char *name;
	union {
		struct object *any;
		struct tree *tree;
		struct blob *blob;
	} item;
	struct tree_entry_list *parent;
};

struct tree {
	struct object object;
	struct tree_entry_list *entries;
};

struct tree *lookup_tree(const unsigned char *sha1);

int parse_tree_buffer(struct tree *item, void *buffer, unsigned long size);

int parse_tree(struct tree *tree);

/* Parses and returns the tree in the given ent, chasing tags and commits. */
struct tree *parse_tree_indirect(const unsigned char *sha1);

#endif /* TREE_H */
