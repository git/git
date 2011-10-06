#ifndef COMMIT_H
#define COMMIT_H

#include "object.h"
#include "tree.h"
#include "strbuf.h"
#include "decorate.h"

struct commit_list {
	struct commit *item;
	struct commit_list *next;
};

struct commit {
	struct object object;
	void *util;
	unsigned int indegree;
	unsigned long date;
	struct commit_list *parents;
	struct tree *tree;
	char *buffer;
};

extern int save_commit_buffer;
extern const char *commit_type;

/* While we can decorate any object with a name, it's only used for commits.. */
extern struct decoration name_decoration;
struct name_decoration {
	struct name_decoration *next;
	int type;
	char name[1];
};

struct commit *lookup_commit(const unsigned char *sha1);
struct commit *lookup_commit_reference(const unsigned char *sha1);
struct commit *lookup_commit_reference_gently(const unsigned char *sha1,
					      int quiet);
struct commit *lookup_commit_reference_by_name(const char *name);

/*
 * Look up object named by "sha1", dereference tag as necessary,
 * get a commit and return it. If "sha1" does not dereference to
 * a commit, use ref_name to report an error and die.
 */
struct commit *lookup_commit_or_die(const unsigned char *sha1, const char *ref_name);

int parse_commit_buffer(struct commit *item, const void *buffer, unsigned long size);
int parse_commit(struct commit *item);

/* Find beginning and length of commit subject. */
int find_commit_subject(const char *commit_buffer, const char **subject);

struct commit_list *commit_list_insert(struct commit *item,
					struct commit_list **list);
unsigned commit_list_count(const struct commit_list *l);
struct commit_list *commit_list_insert_by_date(struct commit *item,
				    struct commit_list **list);
void commit_list_sort_by_date(struct commit_list **list);

void free_commit_list(struct commit_list *list);

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
	CMIT_FMT_USERFORMAT,

	CMIT_FMT_UNSPECIFIED
};

struct pretty_print_context {
	enum cmit_fmt fmt;
	int abbrev;
	const char *subject;
	const char *after_subject;
	int preserve_subject;
	enum date_mode date_mode;
	int need_8bit_cte;
	int show_notes;
	struct reflog_walk_info *reflog_info;
	const char *output_encoding;
};

struct userformat_want {
	unsigned notes:1;
};

extern int has_non_ascii(const char *text);
struct rev_info; /* in revision.h, it circularly uses enum cmit_fmt */
extern char *logmsg_reencode(const struct commit *commit,
			     const char *output_encoding);
extern char *reencode_commit_message(const struct commit *commit,
				     const char **encoding_p);
extern void get_commit_format(const char *arg, struct rev_info *);
extern const char *format_subject(struct strbuf *sb, const char *msg,
				  const char *line_separator);
extern void userformat_find_requirements(const char *fmt, struct userformat_want *w);
extern void format_commit_message(const struct commit *commit,
				  const char *format, struct strbuf *sb,
				  const struct pretty_print_context *context);
extern void pretty_print_commit(const struct pretty_print_context *pp,
				const struct commit *commit,
				struct strbuf *sb);
extern void pp_commit_easy(enum cmit_fmt fmt, const struct commit *commit,
			   struct strbuf *sb);
void pp_user_info(const struct pretty_print_context *pp,
		  const char *what, struct strbuf *sb,
		  const char *line, const char *encoding);
void pp_title_line(const struct pretty_print_context *pp,
		   const char **msg_p,
		   struct strbuf *sb,
		   const char *encoding,
		   int need_8bit_cte);
void pp_remainder(const struct pretty_print_context *pp,
		  const char **msg_p,
		  struct strbuf *sb,
		  int indent);


/** Removes the first commit from a list sorted by date, and adds all
 * of its parents.
 **/
struct commit *pop_most_recent_commit(struct commit_list **list,
				      unsigned int mark);

struct commit *pop_commit(struct commit_list **stack);

void clear_commit_marks(struct commit *commit, unsigned int mark);

/*
 * Performs an in-place topological sort of list supplied.
 *
 *   invariant of resulting list is:
 *      a reachable from b => ord(b) < ord(a)
 *   in addition, when lifo == 0, commits on parallel tracks are
 *   sorted in the dates order.
 */
void sort_in_topological_order(struct commit_list ** list, int lifo);

struct commit_graft {
	unsigned char sha1[20];
	int nr_parent; /* < 0 if shallow commit */
	unsigned char parent[FLEX_ARRAY][20]; /* more */
};
typedef int (*each_commit_graft_fn)(const struct commit_graft *, void *);

struct commit_graft *read_graft_line(char *buf, int len);
int register_commit_graft(struct commit_graft *, int);
struct commit_graft *lookup_commit_graft(const unsigned char *sha1);

extern struct commit_list *get_merge_bases(struct commit *rev1, struct commit *rev2, int cleanup);
extern struct commit_list *get_merge_bases_many(struct commit *one, int n, struct commit **twos, int cleanup);
extern struct commit_list *get_octopus_merge_bases(struct commit_list *in);

extern int register_shallow(const unsigned char *sha1);
extern int unregister_shallow(const unsigned char *sha1);
extern int for_each_commit_graft(each_commit_graft_fn, void *);
extern int is_repository_shallow(void);
extern struct commit_list *get_shallow_commits(struct object_array *heads,
		int depth, int shallow_flag, int not_shallow_flag);

int is_descendant_of(struct commit *, struct commit_list *);
int in_merge_bases(struct commit *, struct commit **, int);

extern int interactive_add(int argc, const char **argv, const char *prefix, int patch);
extern int run_add_interactive(const char *revision, const char *patch_mode,
			       const char **pathspec);

static inline int single_parent(struct commit *commit)
{
	return commit->parents && !commit->parents->next;
}

struct commit_list *reduce_heads(struct commit_list *heads);

extern int commit_tree(const char *msg, unsigned char *tree,
		struct commit_list *parents, unsigned char *ret,
		const char *author);

#endif /* COMMIT_H */
