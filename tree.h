#ifndef TREE_H
#define TREE_H

#include "object.h"

extern const char *tree_type;

struct tree {
	struct object object;
	unsigned has_full_path : 1;
};

struct tree *lookup_tree(unsigned char *sha1);

int parse_tree(struct tree *tree);

#endif /* TREE_H */
