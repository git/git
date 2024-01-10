#ifndef LIST_OBJECTS_H
#define LIST_OBJECTS_H

struct commit;
struct object;
struct rev_info;

typedef void (*show_commit_fn)(struct commit *, void *);
typedef void (*show_object_fn)(struct object *, const char *, void *);

typedef void (*show_edge_fn)(struct commit *);
void mark_edges_uninteresting(struct rev_info *revs,
			      show_edge_fn show_edge,
			      int sparse);

struct oidset;
struct list_objects_filter_options;

void traverse_commit_list_filtered(
	struct rev_info *revs,
	show_commit_fn show_commit,
	show_object_fn show_object,
	void *show_data,
	struct oidset *omitted);

static inline void traverse_commit_list(
	struct rev_info *revs,
	show_commit_fn show_commit,
	show_object_fn show_object,
	void *show_data)
{
	traverse_commit_list_filtered(revs, show_commit,
				      show_object, show_data, NULL);
}

#endif /* LIST_OBJECTS_H */
