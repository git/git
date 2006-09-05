#ifndef LIST_OBJECTS_H
#define LIST_OBJECTS_H

void traverse_commit_list(struct rev_info *revs,
			  void (*show_commit)(struct commit *),
			  void (*show_object)(struct object_array_entry *));

#endif
