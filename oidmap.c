#include "cache.h"
#include "oidmap.h"

static int oidmap_neq(const void *hashmap_cmp_fn_data,
		      const struct hashmap_entry *e1,
		      const struct hashmap_entry *e2,
		      const void *keydata)
{
	const struct oidmap_entry *a, *b;

	a = container_of(e1, const struct oidmap_entry, internal_entry);
	b = container_of(e2, const struct oidmap_entry, internal_entry);

	if (keydata)
		return !oideq(&a->oid, (const struct object_id *) keydata);
	return !oideq(&a->oid, &b->oid);
}

void oidmap_init(struct oidmap *map, size_t initial_size)
{
	hashmap_init(&map->map, oidmap_neq, NULL, initial_size);
}

void oidmap_free(struct oidmap *map, int free_entries)
{
	if (!map)
		return;

	/* TODO: make oidmap itself not depend on struct layouts */
	hashmap_free_(&map->map, free_entries ? 0 : -1);
}

void *oidmap_get(const struct oidmap *map, const struct object_id *key)
{
	if (!map->map.cmpfn)
		return NULL;

	return hashmap_get_from_hash(&map->map, oidhash(key), key);
}

void *oidmap_remove(struct oidmap *map, const struct object_id *key)
{
	struct hashmap_entry entry;

	if (!map->map.cmpfn)
		oidmap_init(map, 0);

	hashmap_entry_init(&entry, oidhash(key));
	return hashmap_remove(&map->map, &entry, key);
}

void *oidmap_put(struct oidmap *map, void *entry)
{
	struct oidmap_entry *to_put = entry;

	if (!map->map.cmpfn)
		oidmap_init(map, 0);

	hashmap_entry_init(&to_put->internal_entry, oidhash(&to_put->oid));
	return hashmap_put(&map->map, &to_put->internal_entry);
}
