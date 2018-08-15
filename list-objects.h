#ifndef LIST_OBJECTS_H
#define LIST_OBJECTS_H

struct commit;
struct object;
struct rev_info;

typedef void (*show_commit_fn)(struct commit *, void *);
typedef void (*show_object_fn)(struct object *, const char *, void *);
void traverse_commit_list(struct rev_info *, show_commit_fn, show_object_fn, void *);

typedef void (*show_edge_fn)(struct commit *);
void mark_edges_uninteresting(struct rev_info *, show_edge_fn);

struct oidset;
struct list_objects_filter_options;

void traverse_commit_list_filtered(
	struct list_objects_filter_options *filter_options,
	struct rev_info *revs,
	show_commit_fn show_commit,
	show_object_fn show_object,
	void *show_data,
	struct oidset *omitted);

#endif /* LIST_OBJECTS_H */
