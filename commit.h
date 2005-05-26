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
	char *buffer;
};

extern const char *commit_type;

struct commit *lookup_commit(unsigned char *sha1);
struct commit *lookup_commit_reference(unsigned char *sha1);

int parse_commit_buffer(struct commit *item, void *buffer, unsigned long size);

int parse_commit(struct commit *item);

void commit_list_insert(struct commit *item, struct commit_list **list_p);

void free_commit_list(struct commit_list *list);

void sort_by_date(struct commit_list **list);

/** Removes the first commit from a list sorted by date, and adds all
 * of its parents.
 **/
struct commit *pop_most_recent_commit(struct commit_list **list, 
				      unsigned int mark);

#endif /* COMMIT_H */
