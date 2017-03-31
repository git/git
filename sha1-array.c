#include "cache.h"
#include "sha1-array.h"
#include "sha1-lookup.h"

void sha1_array_append(struct sha1_array *array, const struct object_id *oid)
{
	ALLOC_GROW(array->oid, array->nr + 1, array->alloc);
	oidcpy(&array->oid[array->nr++], oid);
	array->sorted = 0;
}

static int void_hashcmp(const void *a, const void *b)
{
	return oidcmp(a, b);
}

static void sha1_array_sort(struct sha1_array *array)
{
	QSORT(array->oid, array->nr, void_hashcmp);
	array->sorted = 1;
}

static const unsigned char *sha1_access(size_t index, void *table)
{
	struct object_id *array = table;
	return array[index].hash;
}

int sha1_array_lookup(struct sha1_array *array, const struct object_id *oid)
{
	if (!array->sorted)
		sha1_array_sort(array);
	return sha1_pos(oid->hash, array->oid, array->nr, sha1_access);
}

void sha1_array_clear(struct sha1_array *array)
{
	free(array->oid);
	array->oid = NULL;
	array->nr = 0;
	array->alloc = 0;
	array->sorted = 0;
}

int sha1_array_for_each_unique(struct sha1_array *array,
				for_each_oid_fn fn,
				void *data)
{
	int i;

	if (!array->sorted)
		sha1_array_sort(array);

	for (i = 0; i < array->nr; i++) {
		int ret;
		if (i > 0 && !oidcmp(array->oid + i, array->oid + i - 1))
			continue;
		ret = fn(array->oid + i, data);
		if (ret)
			return ret;
	}
	return 0;
}
