#include "git-compat-util.h"
#include "oid-array.h"
#include "hash-lookup.h"

void oid_array_append(struct oid_array *array, const struct object_id *oid)
{
	ALLOC_GROW(array->oid, array->nr + 1, array->alloc);
	oidcpy(&array->oid[array->nr++], oid);
	if (!oid->algo)
		oid_set_algo(&array->oid[array->nr - 1], the_hash_algo);
	array->sorted = 0;
}

static int void_hashcmp(const void *va, const void *vb)
{
	const struct object_id *a = va, *b = vb;
	int ret;
	if (a->algo == b->algo)
		ret = oidcmp(a, b);
	else
		ret = a->algo > b->algo ? 1 : -1;
	return ret;
}

void oid_array_sort(struct oid_array *array)
{
	if (array->sorted)
		return;
	QSORT(array->oid, array->nr, void_hashcmp);
	array->sorted = 1;
}

static const struct object_id *oid_access(size_t index, const void *table)
{
	const struct object_id *array = table;
	return &array[index];
}

int oid_array_lookup(struct oid_array *array, const struct object_id *oid)
{
	oid_array_sort(array);
	return oid_pos(oid, array->oid, array->nr, oid_access);
}

void oid_array_clear(struct oid_array *array)
{
	FREE_AND_NULL(array->oid);
	array->nr = 0;
	array->alloc = 0;
	array->sorted = 0;
}


int oid_array_for_each(struct oid_array *array,
		       for_each_oid_fn fn,
		       void *data)
{
	size_t i;

	/* No oid_array_sort() here! See oid-array.h */

	for (i = 0; i < array->nr; i++) {
		int ret = fn(array->oid + i, data);
		if (ret)
			return ret;
	}
	return 0;
}

int oid_array_for_each_unique(struct oid_array *array,
			      for_each_oid_fn fn,
			      void *data)
{
	size_t i;

	oid_array_sort(array);

	for (i = 0; i < array->nr; i = oid_array_next_unique(array, i)) {
		int ret = fn(array->oid + i, data);
		if (ret)
			return ret;
	}
	return 0;
}

void oid_array_filter(struct oid_array *array,
		      for_each_oid_fn want,
		      void *cb_data)
{
	size_t nr = array->nr, src, dst;
	struct object_id *oids = array->oid;

	for (src = dst = 0; src < nr; src++) {
		if (want(&oids[src], cb_data)) {
			if (src != dst)
				oidcpy(&oids[dst], &oids[src]);
			dst++;
		}
	}
	array->nr = dst;
}
