#ifndef OIDMAP_H
#define OIDMAP_H

#include "hash.h"
#include "hashmap.h"

/*
 * struct oidmap_entry is a structure representing an entry in the hash table,
 * which must be used as first member of user data structures.
 *
 * Users should set the oid field. oidmap_put() will populate the
 * internal_entry field.
 */
struct oidmap_entry {
	/* For internal use only */
	struct hashmap_entry internal_entry;

	struct object_id oid;
};

struct oidmap {
	struct hashmap map;
};

#define OIDMAP_INIT { { NULL } }

/*
 * Initializes an oidmap structure.
 *
 * `map` is the oidmap to initialize.
 *
 * If the total number of entries is known in advance, the `initial_size`
 * parameter may be used to preallocate a sufficiently large table and thus
 * prevent expensive resizing. If 0, the table is dynamically resized.
 */
void oidmap_init(struct oidmap *map, size_t initial_size);

/*
 * Frees an oidmap structure and allocated memory.
 *
 * If `free_entries` is true, each oidmap_entry in the map is freed as well
 * using stdlibs free().
 */
void oidmap_free(struct oidmap *map, int free_entries);

/*
 * Returns the oidmap entry for the specified oid, or NULL if not found.
 */
void *oidmap_get(const struct oidmap *map,
		 const struct object_id *key);

/*
 * Adds or replaces an oidmap entry.
 *
 * ((struct oidmap_entry *) entry)->internal_entry will be populated by this
 * function.
 *
 * Returns the replaced entry, or NULL if not found (i.e. the entry was added).
 */
void *oidmap_put(struct oidmap *map, void *entry);

/*
 * Removes an oidmap entry matching the specified oid.
 *
 * Returns the removed entry, or NULL if not found.
 */
void *oidmap_remove(struct oidmap *map, const struct object_id *key);


struct oidmap_iter {
	struct hashmap_iter h_iter;
};

static inline void oidmap_iter_init(struct oidmap *map, struct oidmap_iter *iter)
{
	hashmap_iter_init(&map->map, &iter->h_iter);
}

static inline void *oidmap_iter_next(struct oidmap_iter *iter)
{
	/* TODO: this API could be reworked to do compile-time type checks */
	return (void *)hashmap_iter_next(&iter->h_iter);
}

static inline void *oidmap_iter_first(struct oidmap *map,
				      struct oidmap_iter *iter)
{
	oidmap_iter_init(map, iter);
	/* TODO: this API could be reworked to do compile-time type checks */
	return (void *)oidmap_iter_next(iter);
}

#endif
