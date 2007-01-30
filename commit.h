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
	void *util;
	unsigned long date;
	struct commit_list *parents;
	struct tree *tree;
	char *buffer;
};

extern int save_commit_buffer;
extern const char *commit_type;

struct commit *lookup_commit(const unsigned char *sha1);
struct commit *lookup_commit_reference(const unsigned char *sha1);
struct commit *lookup_commit_reference_gently(const unsigned char *sha1,
					      int quiet);

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
	CMIT_FMT_FULLER,
	CMIT_FMT_ONELINE,
	CMIT_FMT_EMAIL,

	CMIT_FMT_UNSPECIFIED,
};

extern enum cmit_fmt get_commit_format(const char *arg);
extern unsigned long pretty_print_commit(enum cmit_fmt fmt, const struct commit *, unsigned long len, char *buf, unsigned long space, int abbrev, const char *subject, const char *after_subject, int relative_date);

/** Removes the first commit from a list sorted by date, and adds all
 * of its parents.
 **/
struct commit *pop_most_recent_commit(struct commit_list **list, 
				      unsigned int mark);

struct commit *pop_commit(struct commit_list **stack);

void clear_commit_marks(struct commit *commit, unsigned int mark);

int count_parents(struct commit * commit);

/*
 * Performs an in-place topological sort of list supplied.
 *
 * Pre-conditions for sort_in_topological_order:
 *   all commits in input list and all parents of those
 *   commits must have object.util == NULL
 *
 * Pre-conditions for sort_in_topological_order_fn:
 *   all commits in input list and all parents of those
 *   commits must have getter(commit) == NULL
 *
 * Post-conditions:
 *   invariant of resulting list is:
 *      a reachable from b => ord(b) < ord(a)
 *   in addition, when lifo == 0, commits on parallel tracks are
 *   sorted in the dates order.
 */

typedef void (*topo_sort_set_fn_t)(struct commit*, void *data);
typedef void* (*topo_sort_get_fn_t)(struct commit*);

void topo_sort_default_setter(struct commit *c, void *data);
void *topo_sort_default_getter(struct commit *c);

void sort_in_topological_order(struct commit_list ** list, int lifo);
void sort_in_topological_order_fn(struct commit_list ** list, int lifo,
				  topo_sort_set_fn_t setter,
				  topo_sort_get_fn_t getter);

struct commit_graft {
	unsigned char sha1[20];
	int nr_parent; /* < 0 if shallow commit */
	unsigned char parent[FLEX_ARRAY][20]; /* more */
};

struct commit_graft *read_graft_line(char *buf, int len);
int register_commit_graft(struct commit_graft *, int);
int read_graft_file(const char *graft_file);

extern struct commit_list *get_merge_bases(struct commit *rev1, struct commit *rev2, int cleanup);

extern int register_shallow(const unsigned char *sha1);
extern int unregister_shallow(const unsigned char *sha1);
extern int write_shallow_commits(int fd, int use_pack_protocol);
extern int is_repository_shallow(void);
extern struct commit_list *get_shallow_commits(struct object_array *heads,
		int depth, int shallow_flag, int not_shallow_flag);

int in_merge_bases(struct commit *rev1, struct commit *rev2);
#endif /* COMMIT_H */
