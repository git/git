#ifndef COMMIT_H
#define COMMIT_H

#include "object.h"
#include "tree.h"
#include "strbuf.h"
#include "decorate.h"
#include "gpg-interface.h"
#include "string-list.h"
#include "pretty.h"
#include "commit-slab.h"

#define COMMIT_NOT_FROM_GRAPH 0xFFFFFFFF
#define GENERATION_NUMBER_INFINITY ((1ULL << 63) - 1)
#define GENERATION_NUMBER_V1_MAX 0x3FFFFFFF
#define GENERATION_NUMBER_ZERO 0
#define GENERATION_NUMBER_V2_OFFSET_MAX ((1ULL << 31) - 1)

struct commit_list {
	struct commit *item;
	struct commit_list *next;
};

/*
 * The size of this struct matters in full repo walk operations like
 * 'git clone' or 'git gc'. Consider using commit-slab to attach data
 * to a commit instead of adding new fields here.
 */
struct commit {
	struct object object;
	timestamp_t date;
	struct commit_list *parents;

	/*
	 * If the commit is loaded from the commit-graph file, then this
	 * member may be NULL. Only access it through repo_get_commit_tree()
	 * or get_commit_tree_oid().
	 */
	struct tree *maybe_tree;
	unsigned int index;
};

extern int save_commit_buffer;
extern int no_graft_file_deprecated_advice;
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

/*
 * Look up commit named by "oid" respecting replacement objects.
 * Returns NULL if "oid" is not a commit or does not exist.
 */
struct commit *lookup_commit_object(struct repository *r, const struct object_id *oid);

/*
 * Look up commit named by "oid" without replacement objects or
 * checking for object existence. Returns the requested commit if it
 * is found in the object cache, NULL if "oid" is in the object cache
 * but is not a commit and a newly allocated unparsed commit object if
 * "oid" is not in the object cache.
 */
struct commit *lookup_commit(struct repository *r, const struct object_id *oid);
struct commit *lookup_commit_reference(struct repository *r,
				       const struct object_id *oid);
struct commit *lookup_commit_reference_gently(struct repository *r,
					      const struct object_id *oid,
					      int quiet);
struct commit *lookup_commit_reference_by_name(const char *name);

/*
 * Look up object named by "oid", dereference tag as necessary,
 * get a commit and return it. If "oid" does not dereference to
 * a commit, use ref_name to report an error and die.
 */
struct commit *lookup_commit_or_die(const struct object_id *oid, const char *ref_name);

int parse_commit_buffer(struct repository *r, struct commit *item, const void *buffer, unsigned long size, int check_graph);
int repo_parse_commit_internal(struct repository *r, struct commit *item,
			       int quiet_on_missing, int use_commit_graph);
int repo_parse_commit_gently(struct repository *r,
			     struct commit *item,
			     int quiet_on_missing);
static inline int repo_parse_commit(struct repository *r, struct commit *item)
{
	return repo_parse_commit_gently(r, item, 0);
}

static inline int repo_parse_commit_no_graph(struct repository *r,
					     struct commit *commit)
{
	return repo_parse_commit_internal(r, commit, 0, 0);
}

void parse_commit_or_die(struct commit *item);

struct buffer_slab;
struct buffer_slab *allocate_commit_buffer_slab(void);
void free_commit_buffer_slab(struct buffer_slab *bs);

/*
 * Associate an object buffer with the commit. The ownership of the
 * memory is handed over to the commit, and must be free()-able.
 */
void set_commit_buffer(struct repository *r, struct commit *, void *buffer, unsigned long size);

/*
 * Get any cached object buffer associated with the commit. Returns NULL
 * if none. The resulting memory should not be freed.
 */
const void *get_cached_commit_buffer(struct repository *, const struct commit *, unsigned long *size);

/*
 * Get the commit's object contents, either from cache or by reading the object
 * from disk. The resulting memory should not be modified, and must be given
 * to repo_unuse_commit_buffer when the caller is done.
 */
const void *repo_get_commit_buffer(struct repository *r,
				   const struct commit *,
				   unsigned long *size);

/*
 * Tell the commit subsystem that we are done with a particular commit buffer.
 * The commit and buffer should be the input and return value, respectively,
 * from an earlier call to repo_get_commit_buffer.  The buffer may or may not be
 * freed by this call; callers should not access the memory afterwards.
 */
void repo_unuse_commit_buffer(struct repository *r,
			      const struct commit *,
			      const void *buffer);

/*
 * Free any cached object buffer associated with the commit.
 */
void free_commit_buffer(struct parsed_object_pool *pool, struct commit *);

struct tree *repo_get_commit_tree(struct repository *, const struct commit *);
struct object_id *get_commit_tree_oid(const struct commit *);

/*
 * Release memory related to a commit, including the parent list and
 * any cached object buffer.
 */
void release_commit_memory(struct parsed_object_pool *pool, struct commit *c);

/*
 * Disassociate any cached object buffer from the commit, but do not free it.
 * The buffer (or NULL, if none) is returned.
 */
const void *detach_commit_buffer(struct commit *, unsigned long *sizep);

/* Find beginning and length of commit subject. */
int find_commit_subject(const char *commit_buffer, const char **subject);

/* Return length of the commit subject from commit log message. */
size_t commit_subject_length(const char *body);

struct commit_list *commit_list_insert(struct commit *item,
					struct commit_list **list);
int commit_list_contains(struct commit *item,
			 struct commit_list *list);
struct commit_list **commit_list_append(struct commit *commit,
					struct commit_list **next);
unsigned commit_list_count(const struct commit_list *l);
struct commit_list *commit_list_insert_by_date(struct commit *item,
				    struct commit_list **list);
void commit_list_sort_by_date(struct commit_list **list);

/* Shallow copy of the input list */
struct commit_list *copy_commit_list(struct commit_list *list);

/* Modify list in-place to reverse it, returning new head; list will be tail */
struct commit_list *reverse_commit_list(struct commit_list *list);

void free_commit_list(struct commit_list *list);

struct rev_info; /* in revision.h, it circularly uses enum cmit_fmt */

const char *repo_logmsg_reencode(struct repository *r,
				 const struct commit *commit,
				 char **commit_encoding,
				 const char *output_encoding);

const char *skip_blank_lines(const char *msg);

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
/* commit_graft_pos returns an index into r->parsed_objects->grafts. */
int commit_graft_pos(struct repository *r, const struct object_id *oid);
int register_commit_graft(struct repository *r, struct commit_graft *, int);
void prepare_commit_graft(struct repository *r);
struct commit_graft *lookup_commit_graft(struct repository *r, const struct object_id *oid);
void reset_commit_grafts(struct repository *r);

struct commit *get_fork_point(const char *refname, struct commit *commit);

/* largest positive number a signed 32-bit integer can contain */
#define INFINITE_DEPTH 0x7fffffff

struct oid_array;
struct ref;
int for_each_commit_graft(each_commit_graft_fn, void *);

int interactive_add(const char **argv, const char *prefix, int patch);

struct commit_extra_header {
	struct commit_extra_header *next;
	char *key;
	char *value;
	size_t len;
};

void append_merge_tag_headers(struct commit_list *parents,
			      struct commit_extra_header ***tail);

int commit_tree(const char *msg, size_t msg_len,
		const struct object_id *tree,
		struct commit_list *parents, struct object_id *ret,
		const char *author, const char *sign_commit);

int commit_tree_extended(const char *msg, size_t msg_len,
			 const struct object_id *tree,
			 struct commit_list *parents, struct object_id *ret,
			 const char *author, const char *committer,
			 const char *sign_commit, struct commit_extra_header *);

struct commit_extra_header *read_commit_extra_headers(struct commit *, const char **);

void free_commit_extra_headers(struct commit_extra_header *extra);

/*
 * Search the commit object contents given by "msg" for the header "key".
 * Reads up to "len" bytes of "msg".
 * Returns a pointer to the start of the header contents, or NULL. The length
 * of the header, up to the first newline, is returned via out_len.
 *
 * Note that some headers (like mergetag) may be multi-line. It is the caller's
 * responsibility to parse further in this case!
 */
const char *find_header_mem(const char *msg, size_t len,
			const char *key,
			size_t *out_len);

const char *find_commit_header(const char *msg, const char *key,
			       size_t *out_len);

/* Find the end of the log message, the right place for a new trailer. */
size_t ignore_non_trailer(const char *buf, size_t len);

typedef int (*each_mergetag_fn)(struct commit *commit, struct commit_extra_header *extra,
				void *cb_data);

int for_each_mergetag(each_mergetag_fn fn, struct commit *commit, void *data);

struct merge_remote_desc {
	struct object *obj; /* the named object, could be a tag */
	char name[FLEX_ARRAY];
};
struct merge_remote_desc *merge_remote_util(struct commit *);
void set_merge_remote_desc(struct commit *commit,
			   const char *name, struct object *obj);

/*
 * Given "name" from the command line to merge, find the commit object
 * and return it, while storing merge_remote_desc in its ->util field,
 * to allow callers to tell if we are told to merge a tag.
 */
struct commit *get_merge_parent(const char *name);

int parse_signed_commit(const struct commit *commit,
			struct strbuf *message, struct strbuf *signature,
			const struct git_hash_algo *algop);
int remove_signature(struct strbuf *buf);

/*
 * Check the signature of the given commit. The result of the check is stored
 * in sig->check_result, 'G' for a good signature, 'U' for a good signature
 * from an untrusted signer, 'B' for a bad signature and 'N' for no signature
 * at all.  This may allocate memory for sig->gpg_output, sig->gpg_status,
 * sig->signer and sig->key.
 */
int check_commit_signature(const struct commit *commit, struct signature_check *sigc);

/* record author-date for each commit object */
struct author_date_slab;
void record_author_date(struct author_date_slab *author_date,
			struct commit *commit);

int compare_commits_by_author_date(const void *a_, const void *b_, void *unused);

/*
 * Verify a single commit with check_commit_signature() and die() if it is not
 * a good signature. This isn't really suitable for general use, but is a
 * helper to implement consistent logic for pull/merge --verify-signatures.
 *
 * The check_trust parameter is meant for backward-compatibility.  The GPG
 * interface verifies key trust with a default trust level that is below the
 * default trust level for merge operations.  Its value should be non-zero if
 * the user hasn't set a minimum trust level explicitly in their configuration.
 *
 * If the user has set a minimum trust level, then that value should be obeyed
 * and check_trust should be zero, even if the configured trust level is below
 * the default trust level for merges.
 */
void verify_merge_signature(struct commit *commit, int verbose,
			    int check_trust);

int compare_commits_by_commit_date(const void *a_, const void *b_, void *unused);
int compare_commits_by_gen_then_commit_date(const void *a_, const void *b_, void *unused);

LAST_ARG_MUST_BE_NULL
int run_commit_hook(int editor_is_used, const char *index_file,
		    int *invoked_hook, const char *name, ...);

/* Sign a commit or tag buffer, storing the result in a header. */
int sign_with_header(struct strbuf *buf, const char *keyid);
/* Parse the signature out of a header. */
int parse_buffer_signed_by_header(const char *buffer,
				  unsigned long size,
				  struct strbuf *payload,
				  struct strbuf *signature,
				  const struct git_hash_algo *algop);

#endif /* COMMIT_H */
