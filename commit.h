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

struct commit_list * commit_list_insert(struct commit *item, struct commit_list **list_p);

void free_commit_list(struct commit_list *list);

void sort_by_date(struct commit_list **list);

/* Commit formats */
enum cmit_fmt {
	CMIT_FMT_RAW,
	CMIT_FMT_MEDIUM,
	CMIT_FMT_DEFAULT = CMIT_FMT_MEDIUM,
	CMIT_FMT_SHORT
};

extern unsigned long pretty_print_commit(enum cmit_fmt fmt, const char *msg, unsigned long len, char *buf, unsigned long space);

void insert_by_date(struct commit_list **list, struct commit *item);

/** Removes the first commit from a list sorted by date, and adds all
 * of its parents.
 **/
struct commit *pop_most_recent_commit(struct commit_list **list, 
				      unsigned int mark);

struct commit *pop_commit(struct commit_list **stack);

int count_parents(struct commit * commit);
#endif /* COMMIT_H */
