#ifndef COMMIT_H
#define COMMIT_H

#include "object.h"
#include "tree.h"
#include "strbuf.h"
#include "decorate.h"
#include "gpg-interface.h"
#include "string-list.h"
#include "pretty.h"

#define COMMIT_NOT_FROM_GRAPH 0xFFFFFFFF

struct commit_list {
	struct commit *item;
	struct commit_list *next;
};

struct commit {
	struct object object;
	void *util;
	unsigned int index;
	timestamp_t date;
	struct commit_list *parents;
	struct tree *tree;
	uint32_t graph_pos;
};

extern int save_commit_buffer;
extern const char *commit_type;

/* While we can decorate any object with a name, it's only used for commits.. */
struct name_decoration {
	struct name_decoration *next;
	int type;
	char name[FLEX_ARRAY];
};

enum decoration_type {
	DECORATION_NONE = 0,
	DECORATION_REF_LOCAL,
	DECORATION_REF_REMOTE,
	DECORATION_REF_TAG,
	DECORATION_REF_STASH,
	DECORATION_REF_HEAD,
	DECORATION_GRAFTED,
};

void add_name_decoration(enum decoration_type type, const char *name, struct object *obj);
const struct name_decoration *get_name_decoration(const struct object *obj);

struct commit *lookup_commit(const struct object_id *oid);
struct commit *lookup_commit_reference(const struct object_id *oid);
struct commit *lookup_commit_reference_gently(const struct object_id *oid,
					      int quiet);
struct commit *lookup_commit_reference_by_name(const char *name);

/*
 * Look up object named by "oid", dereference tag as necessary,
 * get a commit and return it. If "oid" does not dereference to
 * a commit, use ref_name to report an error and die.
 */
struct commit *lookup_commit_or_die(const struct object_id *oid, const char *ref_name);

int parse_commit_buffer(struct commit *item, const void *buffer, unsigned long size);
int parse_commit_gently(struct commit *item, int quiet_on_missing);
static inline int parse_commit(struct commit *item)
{
	return parse_commit_gently(item, 0);
}
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

struct rev_info; /* in revision.h, it circularly uses enum cmit_fmt */

extern int has_non_ascii(const char *text);
extern const char *logmsg_reencode(const struct commit *commit,
				   char **commit_encoding,
				   const char *output_encoding);
extern const char *skip_blank_lines(const char *msg);

/** Removes the first commit from a list sorted by date, and adds all
 * of its parents.
 **/
struct commit *pop_most_recent_commit(struct commit_list **list,
				      unsigned int mark);

struct commit *pop_commit(struct commit_list **stack);

void clear_commit_marks(struct commit *commit, unsigned int mark);
void clear_commit_marks_many(int nr, struct commit **commit, unsigned int mark);


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
	struct object_id oid;
	int nr_parent; /* < 0 if shallow commit */
	struct object_id parent[FLEX_ARRAY]; /* more */
};
typedef int (*each_commit_graft_fn)(const struct commit_graft *, void *);

struct commit_graft *read_graft_line(struct strbuf *line);
int register_commit_graft(struct commit_graft *, int);
struct commit_graft *lookup_commit_graft(const struct object_id *oid);

extern struct commit_list *get_merge_bases(struct commit *rev1, struct commit *rev2);
extern struct commit_list *get_merge_bases_many(struct commit *one, int n, struct commit **twos);
extern struct commit_list *get_octopus_merge_bases(struct commit_list *in);

/* To be used only when object flags after this call no longer matter */
extern struct commit_list *get_merge_bases_many_dirty(struct commit *one, int n, struct commit **twos);

/* largest positive number a signed 32-bit integer can contain */
#define INFINITE_DEPTH 0x7fffffff

struct oid_array;
struct ref;
extern int register_shallow(const struct object_id *oid);
extern int unregister_shallow(const struct object_id *oid);
extern int for_each_commit_graft(each_commit_graft_fn, void *);
extern int is_repository_shallow(void);
extern struct commit_list *get_shallow_commits(struct object_array *heads,
		int depth, int shallow_flag, int not_shallow_flag);
extern struct commit_list *get_shallow_commits_by_rev_list(
		int ac, const char **av, int shallow_flag, int not_shallow_flag);
extern void set_alternate_shallow_file(const char *path, int override);
extern int write_shallow_commits(struct strbuf *out, int use_pack_protocol,
				 const struct oid_array *extra);
extern void setup_alternate_shallow(struct lock_file *shallow_lock,
				    const char **alternate_shallow_file,
				    const struct oid_array *extra);
extern const char *setup_temporary_shallow(const struct oid_array *extra);
extern void advertise_shallow_grafts(int);

struct shallow_info {
	struct oid_array *shallow;
	int *ours, nr_ours;
	int *theirs, nr_theirs;
	struct oid_array *ref;

	/* for receive-pack */
	uint32_t **used_shallow;
	int *need_reachability_test;
	int *reachable;
	int *shallow_ref;
	struct commit **commits;
	int nr_commits;
};

extern void prepare_shallow_info(struct shallow_info *, struct oid_array *);
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

/*
 * Takes a list of commits and returns a new list where those
 * have been removed that can be reached from other commits in
 * the list. It is useful for, e.g., reducing the commits
 * randomly thrown at the git-merge command and removing
 * redundant commits that the user shouldn't have given to it.
 *
 * This function destroys the STALE bit of the commit objects'
 * flags.
 */
extern struct commit_list *reduce_heads(struct commit_list *heads);

/*
 * Like `reduce_heads()`, except it replaces the list. Use this
 * instead of `foo = reduce_heads(foo);` to avoid memory leaks.
 */
extern void reduce_heads_replace(struct commit_list **heads);

struct commit_extra_header {
	struct commit_extra_header *next;
	char *key;
	char *value;
	size_t len;
};

extern void append_merge_tag_headers(struct commit_list *parents,
				     struct commit_extra_header ***tail);

extern int commit_tree(const char *msg, size_t msg_len,
		       const struct object_id *tree,
		       struct commit_list *parents, struct object_id *ret,
		       const char *author, const char *sign_commit);

extern int commit_tree_extended(const char *msg, size_t msg_len,
				const struct object_id *tree,
				struct commit_list *parents,
				struct object_id *ret, const char *author,
				const char *sign_commit,
				struct commit_extra_header *);

extern struct commit_extra_header *read_commit_extra_headers(struct commit *, const char **);

extern void free_commit_extra_headers(struct commit_extra_header *extra);

/*
 * Search the commit object contents given by "msg" for the header "key".
 * Returns a pointer to the start of the header contents, or NULL. The length
 * of the header, up to the first newline, is returned via out_len.
 *
 * Note that some headers (like mergetag) may be multi-line. It is the caller's
 * responsibility to parse further in this case!
 */
extern const char *find_commit_header(const char *msg, const char *key,
				      size_t *out_len);

/* Find the end of the log message, the right place for a new trailer. */
extern int ignore_non_trailer(const char *buf, size_t len);

typedef void (*each_mergetag_fn)(struct commit *commit, struct commit_extra_header *extra,
				 void *cb_data);

extern void for_each_mergetag(each_mergetag_fn fn, struct commit *commit, void *data);

struct merge_remote_desc {
	struct object *obj; /* the named object, could be a tag */
	char name[FLEX_ARRAY];
};
#define merge_remote_util(commit) ((struct merge_remote_desc *)((commit)->util))
extern void set_merge_remote_desc(struct commit *commit,
				  const char *name, struct object *obj);

/*
 * Given "name" from the command line to merge, find the commit object
 * and return it, while storing merge_remote_desc in its ->util field,
 * to allow callers to tell if we are told to merge a tag.
 */
struct commit *get_merge_parent(const char *name);

extern int parse_signed_commit(const struct commit *commit,
			       struct strbuf *message, struct strbuf *signature);
extern int remove_signature(struct strbuf *buf);

/*
 * Check the signature of the given commit. The result of the check is stored
 * in sig->check_result, 'G' for a good signature, 'U' for a good signature
 * from an untrusted signer, 'B' for a bad signature and 'N' for no signature
 * at all.  This may allocate memory for sig->gpg_output, sig->gpg_status,
 * sig->signer and sig->key.
 */
extern int check_commit_signature(const struct commit *commit, struct signature_check *sigc);

int compare_commits_by_commit_date(const void *a_, const void *b_, void *unused);

LAST_ARG_MUST_BE_NULL
extern int run_commit_hook(int editor_is_used, const char *index_file, const char *name, ...);

#endif /* COMMIT_H */
