#ifndef PREFIX_MAP_H
#define PREFIX_MAP_H

#include "hashmap.h"

struct prefix_item {
	const char *name;
	size_t prefix_length;
};

struct prefix_map_entry {
	struct hashmap_entry e;
	const char *name;
	size_t prefix_length;
	/* if item is NULL, the prefix is not unique */
	struct prefix_item *item;
};

struct prefix_map {
	struct hashmap map;
	int min_length, max_length;
};

/*
 * Find unique prefixes in a given list of strings.
 *
 * Typically, the `struct prefix_item` information will be but a field in the
 * actual item struct; For this reason, the `list` parameter is specified as a
 * list of pointers to the items.
 *
 * The `min_length`/`max_length` parameters define what length the unique
 * prefixes should have.
 *
 * If no unique prefix could be found for a given item, its `prefix_length`
 * will be set to 0.
 */
void find_unique_prefixes(struct prefix_item **list, size_t nr,
			  int min_length, int max_length);

#endif
