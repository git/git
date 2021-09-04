#ifndef UNPACK_TREES_H
#define UNPACK_TREES_H

#include "cache.h"
#include "strvec.h"
#include "string-list.h"
#include "tree-walk.h"

#define MAX_UNPACK_TREES MAX_TRAVERSE_TREES

struct cache_entry;
struct unpack_trees_options;
struct pattern_list;

typedef int (*merge_fn_t)(const struct cache_entry * const *src,
		struct unpack_trees_options *options);

enum unpack_trees_error_types {
	ERROR_WOULD_OVERWRITE = 0,
	ERROR_NOT_UPTODATE_FILE,
	ERROR_NOT_UPTODATE_DIR,
	ERROR_WOULD_LOSE_UNTRACKED_OVERWRITTEN,
	ERROR_WOULD_LOSE_UNTRACKED_REMOVED,
	ERROR_BIND_OVERLAP,
	ERROR_WOULD_LOSE_SUBMODULE,

	NB_UNPACK_TREES_ERROR_TYPES,

	WARNING_SPARSE_NOT_UPTODATE_FILE,
	WARNING_SPARSE_UNMERGED_FILE,
	WARNING_SPARSE_ORPHANED_NOT_OVERWRITTEN,

	NB_UNPACK_TREES_WARNING_TYPES,
};

/*
 * Sets the list of user-friendly error messages to be used by the
 * command "cmd" (either merge or checkout), and show_all_errors to 1.
 */
void setup_unpack_trees_porcelain(struct unpack_trees_options *opts,
				  const char *cmd);

/*
 * Frees resources allocated by setup_unpack_trees_porcelain().
 */
void clear_unpack_trees_porcelain(struct unpack_trees_options *opts);

struct unpack_trees_options {
	unsigned int reset_nuke_untracked,
		     reset_keep_untracked,
		     reset_either, /* internal use only */
		     merge,
		     update,
		     clone,
		     index_only,
		     nontrivial_merge,
		     trivial_merges_only,
		     verbose_update,
		     aggressive,
		     skip_unmerged,
		     initial_checkout,
		     diff_index_cached,
		     debug_unpack,
		     skip_sparse_checkout,
		     quiet,
		     exiting_early,
		     show_all_errors,
		     dry_run;
	const char *prefix;
	int cache_bottom;
	struct dir_struct *dir;
	struct pathspec *pathspec;
	merge_fn_t fn;
	const char *msgs[NB_UNPACK_TREES_WARNING_TYPES];
	struct strvec msgs_to_free;
	/*
	 * Store error messages in an array, each case
	 * corresponding to a error message type
	 */
	struct string_list unpack_rejects[NB_UNPACK_TREES_WARNING_TYPES];

	int head_idx;
	int merge_size;

	struct cache_entry *df_conflict_entry;
	void *unpack_data;

	struct index_state *dst_index;
	struct index_state *src_index;
	struct index_state result;

	struct pattern_list *pl; /* for internal use */
	struct checkout_metadata meta;
};

int unpack_trees(unsigned n, struct tree_desc *t,
		 struct unpack_trees_options *options);

enum update_sparsity_result {
	UPDATE_SPARSITY_SUCCESS = 0,
	UPDATE_SPARSITY_WARNINGS = 1,
	UPDATE_SPARSITY_INDEX_UPDATE_FAILURES = -1,
	UPDATE_SPARSITY_WORKTREE_UPDATE_FAILURES = -2
};

enum update_sparsity_result update_sparsity(struct unpack_trees_options *options);

int verify_uptodate(const struct cache_entry *ce,
		    struct unpack_trees_options *o);

int threeway_merge(const struct cache_entry * const *stages,
		   struct unpack_trees_options *o);
int twoway_merge(const struct cache_entry * const *src,
		 struct unpack_trees_options *o);
int bind_merge(const struct cache_entry * const *src,
	       struct unpack_trees_options *o);
int oneway_merge(const struct cache_entry * const *src,
		 struct unpack_trees_options *o);
int stash_worktree_untracked_merge(const struct cache_entry * const *src,
				   struct unpack_trees_options *o);

#endif
