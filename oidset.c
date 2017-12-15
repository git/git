#include "cache.h"
#include "oidset.h"

int oidset_contains(const struct oidset *set, const struct object_id *oid)
{
	if (!set->map.map.tablesize)
		return 0;
	return !!oidmap_get(&set->map, oid);
}

int oidset_insert(struct oidset *set, const struct object_id *oid)
{
	struct oidmap_entry *entry;

	if (!set->map.map.tablesize)
		oidmap_init(&set->map, 0);
	else if (oidset_contains(set, oid))
		return 1;

	entry = xmalloc(sizeof(*entry));
	oidcpy(&entry->oid, oid);

	oidmap_put(&set->map, entry);
	return 0;
}

int oidset_remove(struct oidset *set, const struct object_id *oid)
{
	struct oidmap_entry *entry;

	entry = oidmap_remove(&set->map, oid);
	free(entry);

	return (entry != NULL);
}

void oidset_clear(struct oidset *set)
{
	oidmap_free(&set->map, 1);
}
