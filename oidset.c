#include "cache.h"
#include "oidset.h"

struct oidset_entry {
	struct hashmap_entry hash;
	struct object_id oid;
};

static int oidset_hashcmp(const void *va, const void *vb,
			  const void *vkey)
{
	const struct oidset_entry *a = va, *b = vb;
	const struct object_id *key = vkey;
	return oidcmp(&a->oid, key ? key : &b->oid);
}

int oidset_contains(const struct oidset *set, const struct object_id *oid)
{
	struct hashmap_entry key;

	if (!set->map.cmpfn)
		return 0;

	hashmap_entry_init(&key, sha1hash(oid->hash));
	return !!hashmap_get(&set->map, &key, oid);
}

int oidset_insert(struct oidset *set, const struct object_id *oid)
{
	struct oidset_entry *entry;

	if (!set->map.cmpfn)
		hashmap_init(&set->map, oidset_hashcmp, 0);

	if (oidset_contains(set, oid))
		return 1;

	entry = xmalloc(sizeof(*entry));
	hashmap_entry_init(&entry->hash, sha1hash(oid->hash));
	oidcpy(&entry->oid, oid);

	hashmap_add(&set->map, entry);
	return 0;
}

void oidset_clear(struct oidset *set)
{
	hashmap_free(&set->map, 1);
}
