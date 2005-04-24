#ifndef TREE_H
#define TREE_H

#include "object.h"

extern const char *tree_type;

struct tree_entry_list {
	struct tree_entry_list *next;
	unsigned directory : 1;
	unsigned executable : 1;
	char *name;
	union {
		struct tree *tree;
		struct blob *blob;
	} item;
};

struct tree {
	struct object object;
	unsigned has_full_path : 1;
	struct tree_entry_list *entries;
};

struct tree *lookup_tree(unsigned char *sha1);

int parse_tree(struct tree *tree);

#endif /* TREE_H */
