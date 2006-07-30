#ifndef UNPACK_TREES_H
#define UNPACK_TREES_H

struct unpack_trees_options;

typedef int (*merge_fn_t)(struct cache_entry **src,
		struct unpack_trees_options *options);

struct unpack_trees_options {
	int reset;
	int merge;
	int update;
	int index_only;
	int nontrivial_merge;
	int trivial_merges_only;
	int verbose_update;
	int aggressive;
	const char *prefix;
	merge_fn_t fn;

	int head_idx;
	int merge_size;

	struct cache_entry *df_conflict_entry;
};

extern int unpack_trees(struct object_list *trees,
		struct unpack_trees_options *options);

#endif
