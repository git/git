#ifndef REVISION_H
#define REVISION_H

#define SEEN		(1u<<0)
#define UNINTERESTING   (1u<<1)
#define TREECHANGE	(1u<<2)
#define SHOWN		(1u<<3)
#define TMP_MARK	(1u<<4) /* for isolated cases; clean after use */
#define BOUNDARY	(1u<<5)
#define BOUNDARY_SHOW	(1u<<6)
#define ADDED		(1u<<7)	/* Parents already parsed and added? */

struct rev_info;
struct log_info;

typedef void (prune_fn_t)(struct rev_info *revs, struct commit *commit);

struct rev_info {
	/* Starting list */
	struct commit_list *commits;
	struct object_array pending;

	/* Basic information */
	const char *prefix;
	void *prune_data;
	prune_fn_t *prune_fn;

	/* Traversal flags */
	unsigned int	dense:1,
			no_merges:1,
			no_walk:1,
			remove_empty_trees:1,
			simplify_history:1,
			lifo:1,
			topo_order:1,
			tag_objects:1,
			tree_objects:1,
			blob_objects:1,
			edge_hint:1,
			limited:1,
			unpacked:1, /* see also ignore_packed below */
			boundary:1,
			parents:1;

	/* Diff flags */
	unsigned int	diff:1,
			full_diff:1,
			show_root_diff:1,
			no_commit_id:1,
			verbose_header:1,
			ignore_merges:1,
			combine_merges:1,
			dense_combined_merges:1,
			always_show_header:1;

	/* Format info */
	unsigned int	shown_one:1,
			abbrev_commit:1,
			relative_date:1;

	const char **ignore_packed; /* pretend objects in these are unpacked */
	int num_ignore_packed;

	unsigned int	abbrev;
	enum cmit_fmt	commit_format;
	struct log_info *loginfo;
	int		nr, total;
	const char	*mime_boundary;
	const char	*message_id;
	const char	*ref_message_id;
	const char	*add_signoff;
	const char	*extra_headers;

	/* Filter by commit log message */
	struct grep_opt	*grep_filter;

	/* special limits */
	int max_count;
	unsigned long max_age;
	unsigned long min_age;

	/* diff info for patches and for paths limiting */
	struct diff_options diffopt;
	struct diff_options pruning;

	topo_sort_set_fn_t topo_setter;
	topo_sort_get_fn_t topo_getter;
};

#define REV_TREE_SAME		0
#define REV_TREE_NEW		1
#define REV_TREE_DIFFERENT	2

/* revision.c */
extern int rev_same_tree_as_empty(struct rev_info *, struct tree *t1);
extern int rev_compare_tree(struct rev_info *, struct tree *t1, struct tree *t2);

extern void init_revisions(struct rev_info *revs, const char *prefix);
extern int setup_revisions(int argc, const char **argv, struct rev_info *revs, const char *def);
extern int handle_revision_arg(const char *arg, struct rev_info *revs,int flags,int cant_be_filename);

extern void prepare_revision_walk(struct rev_info *revs);
extern struct commit *get_revision(struct rev_info *revs);

extern void mark_parents_uninteresting(struct commit *commit);
extern void mark_tree_uninteresting(struct tree *tree);

struct name_path {
	struct name_path *up;
	int elem_len;
	const char *elem;
};

extern void add_object(struct object *obj,
		       struct object_array *p,
		       struct name_path *path,
		       const char *name);

extern void add_pending_object(struct rev_info *revs, struct object *obj, const char *name);

#endif
