#ifndef STRMAP_H
#define STRMAP_H

#include "hashmap.h"

struct strmap {
	struct hashmap map;
	unsigned int strdup_strings:1;
};

struct strmap_entry {
	struct hashmap_entry ent;
	const char *key;
	void *value;
};

int cmp_strmap_entry(const void *hashmap_cmp_fn_data,
		     const struct hashmap_entry *entry1,
		     const struct hashmap_entry *entry2,
		     const void *keydata);

#define STRMAP_INIT { \
			.map = HASHMAP_INIT(cmp_strmap_entry, NULL),  \
			.strdup_strings = 1,                          \
		    }

/*
 * Initialize the members of the strmap.  Any keys added to the strmap will
 * be strdup'ed with their memory managed by the strmap.
 */
void strmap_init(struct strmap *map);

/*
 * Same as strmap_init, but for those who want to control the memory management
 * carefully instead of using the default of strdup_strings=1.
 */
void strmap_init_with_options(struct strmap *map,
			      int strdup_strings);

/*
 * Remove all entries from the map, releasing any allocated resources.
 */
void strmap_clear(struct strmap *map, int free_values);

/*
 * Insert "str" into the map, pointing to "data".
 *
 * If an entry for "str" already exists, its data pointer is overwritten, and
 * the original data pointer returned. Otherwise, returns NULL.
 */
void *strmap_put(struct strmap *map, const char *str, void *data);

/*
 * Return the strmap_entry mapped by "str", or NULL if there is not such
 * an item in map.
 */
struct strmap_entry *strmap_get_entry(struct strmap *map, const char *str);

/*
 * Return the data pointer mapped by "str", or NULL if the entry does not
 * exist.
 */
void *strmap_get(struct strmap *map, const char *str);

/*
 * Return non-zero iff "str" is present in the map. This differs from
 * strmap_get() in that it can distinguish entries with a NULL data pointer.
 */
int strmap_contains(struct strmap *map, const char *str);

/*
 * Remove the given entry from the strmap.  If the string isn't in the
 * strmap, the map is not altered.
 */
void strmap_remove(struct strmap *map, const char *str, int free_value);

/*
 * Return how many entries the strmap has.
 */
static inline unsigned int strmap_get_size(struct strmap *map)
{
	return hashmap_get_size(&map->map);
}

/*
 * Return whether the strmap is empty.
 */
static inline int strmap_empty(struct strmap *map)
{
	return strmap_get_size(map) == 0;
}

/*
 * iterate through @map using @iter, @var is a pointer to a type strmap_entry
 */
#define strmap_for_each_entry(mystrmap, iter, var)	\
	hashmap_for_each_entry(&(mystrmap)->map, iter, var, ent)

#endif /* STRMAP_H */
