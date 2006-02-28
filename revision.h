#ifndef REVISION_H
#define REVISION_H

#define SEEN		(1u<<0)
#define UNINTERESTING   (1u<<1)
#define TREECHANGE	(1u<<2)

struct rev_info {
	/* Starting list */
	struct commit_list *commits;
	struct object_list *pending_objects;

	/* Basic information */
	const char *prefix;
	const char **paths;

	/* Traversal flags */
	unsigned int	dense:1,
			remove_empty_trees:1,
			lifo:1,
			topo_order:1,
			tag_objects:1,
			tree_objects:1,
			blob_objects:1,
			edge_hint:1,
			limited:1,
			unpacked:1;

	/* special limits */
	int max_count;
	unsigned long max_age;
	unsigned long min_age;
};

/* revision.c */
extern int setup_revisions(int argc, const char **argv, struct rev_info *revs, const char *def);
extern void prepare_revision_walk(struct rev_info *revs);
extern struct commit *get_revision(struct rev_info *revs);

extern void mark_parents_uninteresting(struct commit *commit);
extern void mark_tree_uninteresting(struct tree *tree);

struct name_path {
	struct name_path *up;
	int elem_len;
	const char *elem;
};

extern struct object_list **add_object(struct object *obj,
				       struct object_list **p,
				       struct name_path *path,
				       const char *name);

#endif
