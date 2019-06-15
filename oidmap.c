#include "cache.h"
#include "oidmap.h"

static int oidmap_neq(const void *hashmap_cmp_fn_data,
		      const void *entry, const void *entry_or_key,
		      const void *keydata)
{
	const struct oidmap_entry *entry_ = entry;
	if (keydata)
		return !oideq(&entry_->oid, (const struct object_id *) keydata);
	return !oideq(&entry_->oid,
		      &((const struct oidmap_entry *) entry_or_key)->oid);
}

void oidmap_init(struct oidmap *map, size_t initial_size)
{
	hashmap_init(&map->map, oidmap_neq, NULL, initial_size);
}

void oidmap_free(struct oidmap *map, int free_entries)
{
	if (!map)
		return;
	hashmap_free(&map->map, free_entries);
}

void *oidmap_get(const struct oidmap *map, const struct object_id *key)
{
	if (!map->map.cmpfn)
		return NULL;

	return hashmap_get_from_hash(&map->map, sha1hash(key->hash), key);
}

void *oidmap_remove(struct oidmap *map, const struct object_id *key)
{
	struct hashmap_entry entry;

	if (!map->map.cmpfn)
		oidmap_init(map, 0);

	hashmap_entry_init(&entry, sha1hash(key->hash));
	return hashmap_remove(&map->map, &entry, key);
}

void *oidmap_put(struct oidmap *map, void *entry)
{
	struct oidmap_entry *to_put = entry;

	if (!map->map.cmpfn)
		oidmap_init(map, 0);

	hashmap_entry_init(&to_put->internal_entry, sha1hash(to_put->oid.hash));
	return hashmap_put(&map->map, to_put);
}
