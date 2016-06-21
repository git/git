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

struct commit *lookup_commit(const unsigned char *sha1);
struct commit *lookup_commit_reference(const unsigned char *sha1);

int parse_commit_buffer(struct commit *item, void *buffer, unsigned long size);

int parse_commit(struct commit *item);

struct commit_list * commit_list_insert(struct commit *item, struct commit_list **list_p);
struct commit_list * insert_by_date(struct commit *item, struct commit_list **list);

void free_commit_list(struct commit_list *list);

void sort_by_date(struct commit_list **list);

/* Commit formats */
enum cmit_fmt {
	CMIT_FMT_RAW,
	CMIT_FMT_MEDIUM,
	CMIT_FMT_DEFAULT = CMIT_FMT_MEDIUM,
	CMIT_FMT_SHORT,
	CMIT_FMT_FULL,
};

extern enum cmit_fmt get_commit_format(const char *arg);
extern unsigned long pretty_print_commit(enum cmit_fmt fmt, const char *msg, unsigned long len, char *buf, unsigned long space);

/** Removes the first commit from a list sorted by date, and adds all
 * of its parents.
 **/
struct commit *pop_most_recent_commit(struct commit_list **list, 
				      unsigned int mark);

struct commit *pop_commit(struct commit_list **stack);

int count_parents(struct commit * commit);

/*
 * Performs an in-place topological sort of list supplied.
 *
 * Pre-conditions:
 *   all commits in input list and all parents of those
 *   commits must have object.util == NULL
 *        
 * Post-conditions: 
 *   invariant of resulting list is:
 *      a reachable from b => ord(b) < ord(a)
 */
void sort_in_topological_order(struct commit_list ** list);
#endif /* COMMIT_H */
