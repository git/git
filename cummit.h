#ifndef COMMIT_H
#define COMMIT_H

#include "object.h"
#include "tree.h"
#include "strbuf.h"
#include "decorate.h"
#include "gpg-interface.h"
#include "string-list.h"
#include "pretty.h"
#include "cummit-slab.h"

#define CUMMIT_NOT_FROM_GRAPH 0xFFFFFFFF
#define GENERATION_NUMBER_INFINITY ((1ULL << 63) - 1)
#define GENERATION_NUMBER_V1_MAX 0x3FFFFFFF
#define GENERATION_NUMBER_ZERO 0
#define GENERATION_NUMBER_V2_OFFSET_MAX ((1ULL << 31) - 1)

struct cummit_list {
	struct cummit *item;
	struct cummit_list *next;
};

/*
 * The size of this struct matters in full repo walk operations like
 * 'but clone' or 'but gc'. Consider using cummit-slab to attach data
 * to a cummit instead of adding new fields here.
 */
struct cummit {
	struct object object;
	timestamp_t date;
	struct cummit_list *parents;

	/*
	 * If the cummit is loaded from the cummit-graph file, then this
	 * member may be NULL. Only access it through repo_get_cummit_tree()
	 * or get_cummit_tree_oid().
	 */
	struct tree *maybe_tree;
	unsigned int index;
};

extern int save_cummit_buffer;
extern int no_graft_file_deprecated_advice;
extern const char *cummit_type;

/* While we can decorate any object with a name, it's only used for cummits.. */
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

struct cummit *lookup_cummit(struct repository *r, const struct object_id *oid);
struct cummit *lookup_cummit_reference(struct repository *r,
				       const struct object_id *oid);
struct cummit *lookup_cummit_reference_gently(struct repository *r,
					      const struct object_id *oid,
					      int quiet);
struct cummit *lookup_cummit_reference_by_name(const char *name);

/*
 * Look up object named by "oid", dereference tag as necessary,
 * get a cummit and return it. If "oid" does not dereference to
 * a cummit, use ref_name to report an error and die.
 */
struct cummit *lookup_cummit_or_die(const struct object_id *oid, const char *ref_name);

int parse_cummit_buffer(struct repository *r, struct cummit *item, const void *buffer, unsigned long size, int check_graph);
int repo_parse_cummit_internal(struct repository *r, struct cummit *item,
			       int quiet_on_missing, int use_cummit_graph);
int repo_parse_cummit_gently(struct repository *r,
			     struct cummit *item,
			     int quiet_on_missing);
static inline int repo_parse_cummit(struct repository *r, struct cummit *item)
{
	return repo_parse_cummit_gently(r, item, 0);
}

static inline int repo_parse_cummit_no_graph(struct repository *r,
					     struct cummit *cummit)
{
	return repo_parse_cummit_internal(r, cummit, 0, 0);
}

#ifndef NO_THE_REPOSITORY_COMPATIBILITY_MACROS
#define parse_cummit_internal(item, quiet, use) repo_parse_cummit_internal(the_repository, item, quiet, use)
#define parse_cummit(item) repo_parse_cummit(the_repository, item)
#endif

void parse_cummit_or_die(struct cummit *item);

struct buffer_slab;
struct buffer_slab *allocate_cummit_buffer_slab(void);
void free_cummit_buffer_slab(struct buffer_slab *bs);

/*
 * Associate an object buffer with the cummit. The ownership of the
 * memory is handed over to the cummit, and must be free()-able.
 */
void set_cummit_buffer(struct repository *r, struct cummit *, void *buffer, unsigned long size);

/*
 * Get any cached object buffer associated with the cummit. Returns NULL
 * if none. The resulting memory should not be freed.
 */
const void *get_cached_cummit_buffer(struct repository *, const struct cummit *, unsigned long *size);

/*
 * Get the cummit's object contents, either from cache or by reading the object
 * from disk. The resulting memory should not be modified, and must be given
 * to unuse_cummit_buffer when the caller is done.
 */
const void *repo_get_cummit_buffer(struct repository *r,
				   const struct cummit *,
				   unsigned long *size);
#ifndef NO_THE_REPOSITORY_COMPATIBILITY_MACROS
#define get_cummit_buffer(c, s) repo_get_cummit_buffer(the_repository, c, s)
#endif

/*
 * Tell the cummit subsystem that we are done with a particular cummit buffer.
 * The cummit and buffer should be the input and return value, respectively,
 * from an earlier call to get_cummit_buffer.  The buffer may or may not be
 * freed by this call; callers should not access the memory afterwards.
 */
void repo_unuse_cummit_buffer(struct repository *r,
			      const struct cummit *,
			      const void *buffer);
#ifndef NO_THE_REPOSITORY_COMPATIBILITY_MACROS
#define unuse_cummit_buffer(c, b) repo_unuse_cummit_buffer(the_repository, c, b)
#endif

/*
 * Free any cached object buffer associated with the cummit.
 */
void free_cummit_buffer(struct parsed_object_pool *pool, struct cummit *);

struct tree *repo_get_cummit_tree(struct repository *, const struct cummit *);
#define get_cummit_tree(c) repo_get_cummit_tree(the_repository, c)
struct object_id *get_cummit_tree_oid(const struct cummit *);

/*
 * Release memory related to a cummit, including the parent list and
 * any cached object buffer.
 */
void release_cummit_memory(struct parsed_object_pool *pool, struct cummit *c);

/*
 * Disassociate any cached object buffer from the cummit, but do not free it.
 * The buffer (or NULL, if none) is returned.
 */
const void *detach_cummit_buffer(struct cummit *, unsigned long *sizep);

/* Find beginning and length of cummit subject. */
int find_cummit_subject(const char *cummit_buffer, const char **subject);

/* Return length of the cummit subject from cummit log message. */
size_t cummit_subject_length(const char *body);

struct cummit_list *cummit_list_insert(struct cummit *item,
					struct cummit_list **list);
int cummit_list_contains(struct cummit *item,
			 struct cummit_list *list);
struct cummit_list **cummit_list_append(struct cummit *cummit,
					struct cummit_list **next);
unsigned cummit_list_count(const struct cummit_list *l);
struct cummit_list *cummit_list_insert_by_date(struct cummit *item,
				    struct cummit_list **list);
void cummit_list_sort_by_date(struct cummit_list **list);

/* Shallow copy of the input list */
struct cummit_list *copy_cummit_list(struct cummit_list *list);

/* Modify list in-place to reverse it, returning new head; list will be tail */
struct cummit_list *reverse_cummit_list(struct cummit_list *list);

void free_cummit_list(struct cummit_list *list);

struct rev_info; /* in revision.h, it circularly uses enum cmit_fmt */

int has_non_ascii(const char *text);
const char *logmsg_reencode(const struct cummit *cummit,
			    char **cummit_encoding,
			    const char *output_encoding);
const char *repo_logmsg_reencode(struct repository *r,
				 const struct cummit *cummit,
				 char **cummit_encoding,
				 const char *output_encoding);
#ifndef NO_THE_REPOSITORY_COMPATIBILITY_MACROS
#define logmsg_reencode(c, enc, out) repo_logmsg_reencode(the_repository, c, enc, out)
#endif

const char *skip_blank_lines(const char *msg);

/** Removes the first cummit from a list sorted by date, and adds all
 * of its parents.
 **/
struct cummit *pop_most_recent_cummit(struct cummit_list **list,
				      unsigned int mark);

struct cummit *pop_cummit(struct cummit_list **stack);

void clear_cummit_marks(struct cummit *cummit, unsigned int mark);
void clear_cummit_marks_many(int nr, struct cummit **cummit, unsigned int mark);


enum rev_sort_order {
	REV_SORT_IN_GRAPH_ORDER = 0,
	REV_SORT_BY_CUMMIT_DATE,
	REV_SORT_BY_AUTHOR_DATE
};

/*
 * Performs an in-place topological sort of list supplied.
 *
 *   invariant of resulting list is:
 *      a reachable from b => ord(b) < ord(a)
 *   sort_order further specifies:
 *   REV_SORT_IN_GRAPH_ORDER: try to show a cummit on a single-parent
 *                            chain together.
 *   REV_SORT_BY_CUMMIT_DATE: show eligible cummits in cummitter-date order.
 */
void sort_in_topological_order(struct cummit_list **, enum rev_sort_order);

struct cummit_graft {
	struct object_id oid;
	int nr_parent; /* < 0 if shallow cummit */
	struct object_id parent[FLEX_ARRAY]; /* more */
};
typedef int (*each_cummit_graft_fn)(const struct cummit_graft *, void *);

struct cummit_graft *read_graft_line(struct strbuf *line);
/* cummit_graft_pos returns an index into r->parsed_objects->grafts. */
int cummit_graft_pos(struct repository *r, const struct object_id *oid);
int register_cummit_graft(struct repository *r, struct cummit_graft *, int);
void prepare_cummit_graft(struct repository *r);
struct cummit_graft *lookup_cummit_graft(struct repository *r, const struct object_id *oid);
void reset_cummit_grafts(struct repository *r);

struct cummit *get_fork_point(const char *refname, struct cummit *cummit);

/* largest positive number a signed 32-bit integer can contain */
#define INFINITE_DEPTH 0x7fffffff

struct oid_array;
struct ref;
int for_each_cummit_graft(each_cummit_graft_fn, void *);

int interactive_add(const char **argv, const char *prefix, int patch);
int run_add_interactive(const char *revision, const char *patch_mode,
			const struct pathspec *pathspec);

struct cummit_extra_header {
	struct cummit_extra_header *next;
	char *key;
	char *value;
	size_t len;
};

void append_merge_tag_headers(struct cummit_list *parents,
			      struct cummit_extra_header ***tail);

int cummit_tree(const char *msg, size_t msg_len,
		const struct object_id *tree,
		struct cummit_list *parents, struct object_id *ret,
		const char *author, const char *sign_cummit);

int cummit_tree_extended(const char *msg, size_t msg_len,
			 const struct object_id *tree,
			 struct cummit_list *parents, struct object_id *ret,
			 const char *author, const char *cummitter,
			 const char *sign_cummit, struct cummit_extra_header *);

struct cummit_extra_header *read_cummit_extra_headers(struct cummit *, const char **);

void free_cummit_extra_headers(struct cummit_extra_header *extra);

/*
 * Search the cummit object contents given by "msg" for the header "key".
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

typedef int (*each_mergetag_fn)(struct cummit *cummit, struct cummit_extra_header *extra,
				void *cb_data);

int for_each_mergetag(each_mergetag_fn fn, struct cummit *cummit, void *data);

struct merge_remote_desc {
	struct object *obj; /* the named object, could be a tag */
	char name[FLEX_ARRAY];
};
struct merge_remote_desc *merge_remote_util(struct cummit *);
void set_merge_remote_desc(struct cummit *cummit,
			   const char *name, struct object *obj);

/*
 * Given "name" from the command line to merge, find the cummit object
 * and return it, while storing merge_remote_desc in its ->util field,
 * to allow callers to tell if we are told to merge a tag.
 */
struct cummit *get_merge_parent(const char *name);

int parse_signed_cummit(const struct cummit *cummit,
			struct strbuf *message, struct strbuf *signature,
			const struct but_hash_algo *algop);
int remove_signature(struct strbuf *buf);

/*
 * Check the signature of the given cummit. The result of the check is stored
 * in sig->check_result, 'G' for a good signature, 'U' for a good signature
 * from an untrusted signer, 'B' for a bad signature and 'N' for no signature
 * at all.  This may allocate memory for sig->gpg_output, sig->gpg_status,
 * sig->signer and sig->key.
 */
int check_cummit_signature(const struct cummit *cummit, struct signature_check *sigc);

/* record author-date for each cummit object */
struct author_date_slab;
void record_author_date(struct author_date_slab *author_date,
			struct cummit *cummit);

int compare_cummits_by_author_date(const void *a_, const void *b_, void *unused);

/*
 * Verify a single cummit with check_cummit_signature() and die() if it is not
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
void verify_merge_signature(struct cummit *cummit, int verbose,
			    int check_trust);

int compare_cummits_by_cummit_date(const void *a_, const void *b_, void *unused);
int compare_cummits_by_gen_then_cummit_date(const void *a_, const void *b_, void *unused);

LAST_ARG_MUST_BE_NULL
int run_commit_hook(int editor_is_used, const char *index_file,
		    int *invoked_hook, const char *name, ...);

/* Sign a cummit or tag buffer, storing the result in a header. */
int sign_with_header(struct strbuf *buf, const char *keyid);
/* Parse the signature out of a header. */
int parse_buffer_signed_by_header(const char *buffer,
				  unsigned long size,
				  struct strbuf *payload,
				  struct strbuf *signature,
				  const struct but_hash_algo *algop);

#endif /* COMMIT_H */
