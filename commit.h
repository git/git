#ifndef COMMIT_H
#define COMMIT_H

#include "object.h"
#include "tree.h"

struct commit_list {
	struct commit *item;
	struct commit_list *next;
};

struct commit {
	struct object object;
	unsigned long date;
	struct commit_list *parents;
	struct tree *tree;
};

extern const char *commit_type;

struct commit *lookup_commit(unsigned char *sha1);

int parse_commit(struct commit *item);

void free_commit_list(struct commit_list *list);

#endif /* COMMIT_H */
