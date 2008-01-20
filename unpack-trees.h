#ifndef UNPACK_TREES_H
#define UNPACK_TREES_H

struct unpack_trees_options;

typedef int (*merge_fn_t)(struct cache_entry **src,
		struct unpack_trees_options *options,
		int remove);

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
	int pos;
	struct dir_struct *dir;
	merge_fn_t fn;

	int head_idx;
	int merge_size;

	struct cache_entry *df_conflict_entry;
	void *unpack_data;
};

extern int unpack_trees(unsigned n, struct tree_desc *t,
		struct unpack_trees_options *options);

int threeway_merge(struct cache_entry **stages, struct unpack_trees_options *o, int);
int twoway_merge(struct cache_entry **src, struct unpack_trees_options *o, int);
int bind_merge(struct cache_entry **src, struct unpack_trees_options *o, int);
int oneway_merge(struct cache_entry **src, struct unpack_trees_options *o, int);

#endif
