#ifndef COMMIT_H
#define COMMIT_H

#include "object.h"
#include "tree.h"
#include "strbuf.h"
#include "decorate.h"
#include "gpg-interface.h"
#include "string-list.h"

struct commit_list {
	struct commit *item;
	struct commit_list *next;
};

struct commit {
	struct object object;
	void *util;
	unsigned int index;
	unsigned long date;
	struct commit_list *parents;
	struct tree *tree;
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
void parse_commit_or_die(struct commit *item);

/*
 * Associate an object buffer with the commit. The ownership of the
 * memory is handed over to the commit, and must be free()-able.
 */
void set_commit_buffer(struct commit *, void *buffer, unsigned long size);

/*
 * Get any cached object buffer associated with the commit. Returns NULL
 * if none. The resulting memory should not be freed.
 */
const void *get_cached_commit_buffer(const struct commit *, unsigned long *size);

/*
 * Get the commit's object contents, either from cache or by reading the object
 * from disk. The resulting memory should not be modified, and must be given
 * to unuse_commit_buffer when the caller is done.
 */
const void *get_commit_buffer(const struct commit *, unsigned long *size);

/*
 * Tell the commit subsytem that we are done with a particular commit buffer.
 * The commit and buffer should be the input and return value, respectively,
 * from an earlier call to get_commit_buffer.  The buffer may or may not be
 * freed by this call; callers should not access the memory afterwards.
 */
void unuse_commit_buffer(const struct commit *, const void *buffer);

/*
 * Free any cached object buffer associated with the commit.
 */
void free_commit_buffer(struct commit *);

/*
 * Disassociate any cached object buffer from the commit, but do not free it.
 * The buffer (or NULL, if none) is returned.
 */
const void *detach_commit_buffer(struct commit *, unsigned long *sizep);

/* Find beginning and length of commit subject. */
int find_commit_subject(const char *commit_buffer, const char **subject);

struct commit_list *commit_list_insert(struct commit *item,
					struct commit_list **list);
struct commit_list **commit_list_append(struct commit *commit,
					struct commit_list **next);
unsigned commit_list_count(const struct commit_list *l);
struct commit_list *commit_list_insert_by_date(struct commit *item,
				    struct commit_list **list);
void commit_list_sort_by_date(struct commit_list **list);

/* Shallow copy of the input list */
struct commit_list *copy_commit_list(struct commit_list *list);

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
	/*
	 * Callers should tweak these to change the behavior of pp_* functions.
	 */
	enum cmit_fmt fmt;
	int abbrev;
	const char *subject;
	const char *after_subject;
	int preserve_subject;
	enum date_mode date_mode;
	unsigned date_mode_explicit:1;
	int need_8bit_cte;
	char *notes_message;
	struct reflog_walk_info *reflog_info;
	const char *output_encoding;
	struct string_list *mailmap;
	int color;
	struct ident_split *from_ident;

	/*
	 * Fields below here are manipulated internally by pp_* functions and
	 * should not be counted on by callers.
	 */
	struct string_list in_body_headers;
};

struct userformat_want {
	unsigned notes:1;
};

extern int has_non_ascii(const char *text);
struct rev_info; /* in revision.h, it circularly uses enum cmit_fmt */
extern const char *logmsg_reencode(const struct commit *commit,
				   char **commit_encoding,
				   const char *output_encoding);
extern void get_commit_format(const char *arg, struct rev_info *);
extern const char *format_subject(struct strbuf *sb, const char *msg,
				  const char *line_separator);
extern void userformat_find_requirements(const char *fmt, struct userformat_want *w);
extern int commit_format_is_empty(enum cmit_fmt);
extern void format_commit_message(const struct commit *commit,
				  const char *format, struct strbuf *sb,
				  const struct pretty_print_context *context);
extern void pretty_print_commit(struct pretty_print_context *pp,
				const struct commit *commit,
				struct strbuf *sb);
extern void pp_commit_easy(enum cmit_fmt fmt, const struct commit *commit,
			   struct strbuf *sb);
void pp_user_info(struct pretty_print_context *pp,
		  const char *what, struct strbuf *sb,
		  const char *line, const char *encoding);
void pp_title_line(struct pretty_print_context *pp,
		   const char **msg_p,
		   struct strbuf *sb,
		   const char *encoding,
		   int need_8bit_cte);
void pp_remainder(struct pretty_print_context *pp,
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
void clear_commit_marks_many(int nr, struct commit **commit, unsigned int mark);
void clear_commit_marks_for_object_array(struct object_array *a, unsigned mark);


enum rev_sort_order {
	REV_SORT_IN_GRAPH_ORDER = 0,
	REV_SORT_BY_COMMIT_DATE,
	REV_SORT_BY_AUTHOR_DATE
};

/*
 * Performs an in-place topological sort of list supplied.
 *
 *   invariant of resulting list is:
 *      a reachable from b => ord(b) < ord(a)
 *   sort_order further specifies:
 *   REV_SORT_IN_GRAPH_ORDER: try to show a commit on a single-parent
 *                            chain together.
 *   REV_SORT_BY_COMMIT_DATE: show eligible commits in committer-date order.
 */
void sort_in_topological_order(struct commit_list **, enum rev_sort_order);

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

/* largest positive number a signed 32-bit integer can contain */
#define INFINITE_DEPTH 0x7fffffff

struct sha1_array;
struct ref;
extern int register_shallow(const unsigned char *sha1);
extern int unregister_shallow(const unsigned char *sha1);
extern int for_each_commit_graft(each_commit_graft_fn, void *);
extern int is_repository_shallow(void);
extern struct commit_list *get_shallow_commits(struct object_array *heads,
		int depth, int shallow_flag, int not_shallow_flag);
extern void check_shallow_file_for_update(void);
extern void set_alternate_shallow_file(const char *path, int override);
extern int write_shallow_commits(struct strbuf *out, int use_pack_protocol,
				 const struct sha1_array *extra);
extern void setup_alternate_shallow(struct lock_file *shallow_lock,
				    const char **alternate_shallow_file,
				    const struct sha1_array *extra);
extern const char *setup_temporary_shallow(const struct sha1_array *extra);
extern void advertise_shallow_grafts(int);

struct shallow_info {
	struct sha1_array *shallow;
	int *ours, nr_ours;
	int *theirs, nr_theirs;
	struct sha1_array *ref;

	/* for receive-pack */
	uint32_t **used_shallow;
	int *need_reachability_test;
	int *reachable;
	int *shallow_ref;
	struct commit **commits;
	int nr_commits;
};

extern void prepare_shallow_info(struct shallow_info *, struct sha1_array *);
extern void clear_shallow_info(struct shallow_info *);
extern void remove_nonexistent_theirs_shallow(struct shallow_info *);
extern void assign_shallow_commits_to_refs(struct shallow_info *info,
					   uint32_t **used,
					   int *ref_status);
extern int delayed_reachability_test(struct shallow_info *si, int c);
extern void prune_shallow(int show_only);
extern struct trace_key trace_shallow;

int is_descendant_of(struct commit *, struct commit_list *);
int in_merge_bases(struct commit *, struct commit *);
int in_merge_bases_many(struct commit *, int, struct commit **);

extern int interactive_add(int argc, const char **argv, const char *prefix, int patch);
extern int run_add_interactive(const char *revision, const char *patch_mode,
			       const struct pathspec *pathspec);

static inline int single_parent(struct commit *commit)
{
	return commit->parents && !commit->parents->next;
}

struct commit_list *reduce_heads(struct commit_list *heads);

struct commit_extra_header {
	struct commit_extra_header *next;
	char *key;
	char *value;
	size_t len;
};

extern void append_merge_tag_headers(struct commit_list *parents,
				     struct commit_extra_header ***tail);

extern int commit_tree(const char *msg, size_t msg_len,
		       const unsigned char *tree,
		       struct commit_list *parents, unsigned char *ret,
		       const char *author, const char *sign_commit);

extern int commit_tree_extended(const char *msg, size_t msg_len,
				const unsigned char *tree,
				struct commit_list *parents, unsigned char *ret,
				const char *author, const char *sign_commit,
				struct commit_extra_header *);

extern struct commit_extra_header *read_commit_extra_headers(struct commit *, const char **);

extern void free_commit_extra_headers(struct commit_extra_header *extra);

typedef void (*each_mergetag_fn)(struct commit *commit, struct commit_extra_header *extra,
				 void *cb_data);

extern void for_each_mergetag(each_mergetag_fn fn, struct commit *commit, void *data);

struct merge_remote_desc {
	struct object *obj; /* the named object, could be a tag */
	const char *name;
};
#define merge_remote_util(commit) ((struct merge_remote_desc *)((commit)->util))

/*
 * Given "name" from the command line to merge, find the commit object
 * and return it, while storing merge_remote_desc in its ->util field,
 * to allow callers to tell if we are told to merge a tag.
 */
struct commit *get_merge_parent(const char *name);

extern int parse_signed_commit(const struct commit *commit,
			       struct strbuf *message, struct strbuf *signature);
extern int remove_signature(struct strbuf *buf);

extern void print_commit_list(struct commit_list *list,
			      const char *format_cur,
			      const char *format_last);

/*
 * Check the signature of the given commit. The result of the check is stored
 * in sig->check_result, 'G' for a good signature, 'U' for a good signature
 * from an untrusted signer, 'B' for a bad signature and 'N' for no signature
 * at all.  This may allocate memory for sig->gpg_output, sig->gpg_status,
 * sig->signer and sig->key.
 */
extern void check_commit_signature(const struct commit* commit, struct signature_check *sigc);

int compare_commits_by_commit_date(const void *a_, const void *b_, void *unused);

LAST_ARG_MUST_BE_NULL
extern int run_commit_hook(int editor_is_used, const char *index_file, const char *name, ...);

#endif /* COMMIT_H */
