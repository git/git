#ifndef PATH_LIST_H
#define PATH_LIST_H

struct path_list_item {
	char *path;
	void *util;
};
struct path_list
{
	struct path_list_item *items;
	unsigned int nr, alloc;
	unsigned int strdup_paths:1;
};

void print_path_list(const char *text, const struct path_list *p);
void path_list_clear(struct path_list *list, int free_util);

/* Use these functions only on sorted lists: */
int path_list_has_path(const struct path_list *list, const char *path);
struct path_list_item *path_list_insert(const char *path, struct path_list *list);
struct path_list_item *path_list_lookup(const char *path, struct path_list *list);

/* Use these functions only on unsorted lists: */
struct path_list_item *path_list_append(const char *path, struct path_list *list);
void sort_path_list(struct path_list *list);
int unsorted_path_list_has_path(struct path_list *list, const char *path);

#endif /* PATH_LIST_H */
