#ifndef UNPACK_TREES_H
#define UNPACK_TREES_H

#define MAX_UNPACK_TREES 8

struct unpack_trees_options;

typedef int (*merge_fn_t)(struct cache_entry **src,
		struct unpack_trees_options *options);

struct unpack_trees_error_msgs {
	const char *would_overwrite;
	const char *not_uptodate_file;
	const char *not_uptodate_dir;
	const char *would_lose_untracked;
	const char *bind_overlap;
};

struct unpack_trees_options {
	unsigned int reset,
		     merge,
		     update,
		     index_only,
		     nontrivial_merge,
		     trivial_merges_only,
		     verbose_update,
		     aggressive,
		     skip_unmerged,
		     initial_checkout,
		     diff_index_cached,
		     gently;
	const char *prefix;
	int cache_bottom;
	struct dir_struct *dir;
	merge_fn_t fn;
	struct unpack_trees_error_msgs msgs;

	int head_idx;
	int merge_size;

	struct cache_entry *df_conflict_entry;
	void *unpack_data;

	struct index_state *dst_index;
	struct index_state *src_index;
	struct index_state result;
};

extern int unpack_trees(unsigned n, struct tree_desc *t,
		struct unpack_trees_options *options);

int threeway_merge(struct cache_entry **stages, struct unpack_trees_options *o);
int twoway_merge(struct cache_entry **src, struct unpack_trees_options *o);
int bind_merge(struct cache_entry **src, struct unpack_trees_options *o);
int oneway_merge(struct cache_entry **src, struct unpack_trees_options *o);

#endif
