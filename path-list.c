#include <stdio.h>
#include "cache.h"
#include "path-list.h"

/* if there is no exact match, point to the index where the entry could be
 * inserted */
static int get_entry_index(const struct path_list *list, const char *path,
		int *exact_match)
{
	int left = -1, right = list->nr;

	while (left + 1 < right) {
		int middle = (left + right) / 2;
		int compare = strcmp(path, list->items[middle].path);
		if (compare < 0)
			right = middle;
		else if (compare > 0)
			left = middle;
		else {
			*exact_match = 1;
			return middle;
		}
	}

	*exact_match = 0;
	return right;
}

/* returns -1-index if already exists */
static int add_entry(struct path_list *list, const char *path)
{
	int exact_match;
	int index = get_entry_index(list, path, &exact_match);

	if (exact_match)
		return -1 - index;

	if (list->nr + 1 >= list->alloc) {
		list->alloc += 32;
		list->items = xrealloc(list->items, list->alloc
				* sizeof(struct path_list_item));
	}
	if (index < list->nr)
		memmove(list->items + index + 1, list->items + index,
				(list->nr - index)
				* sizeof(struct path_list_item));
	list->items[index].path = list->strdup_paths ?
		strdup(path) : (char *)path;
	list->items[index].util = NULL;
	list->nr++;

	return index;
}

struct path_list_item *path_list_insert(const char *path, struct path_list *list)
{
	int index = add_entry(list, path);

	if (index < 0)
		index = 1 - index;

	return list->items + index;
}

int path_list_has_path(const struct path_list *list, const char *path)
{
	int exact_match;
	get_entry_index(list, path, &exact_match);
	return exact_match;
}

struct path_list_item *path_list_lookup(const char *path, struct path_list *list)
{
	int exact_match, i = get_entry_index(list, path, &exact_match);
	if (!exact_match)
		return NULL;
	return list->items + i;
}

void path_list_clear(struct path_list *list, int free_items)
{
	if (list->items) {
		int i;
		if (free_items)
			for (i = 0; i < list->nr; i++) {
				if (list->strdup_paths)
					free(list->items[i].path);
				free(list->items[i].util);
			}
		free(list->items);
	}
	list->items = NULL;
	list->nr = list->alloc = 0;
}

void print_path_list(const char *text, const struct path_list *p)
{
	int i;
	if ( text )
		printf("%s\n", text);
	for (i = 0; i < p->nr; i++)
		printf("%s:%p\n", p->items[i].path, p->items[i].util);
}

