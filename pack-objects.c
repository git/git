#include "git-compat-util.h"
#include "object.h"
#include "pack.h"
#include "pack-objects.h"
#include "packfile.h"
#include "parse.h"

static uint32_t locate_object_entry_hash(struct packing_data *pdata,
					 const struct object_id *oid,
					 int *found)
{
	uint32_t i, mask = (pdata->index_size - 1);

	i = oidhash(oid) & mask;

	while (pdata->index[i] > 0) {
		uint32_t pos = pdata->index[i] - 1;

		if (oideq(oid, &pdata->objects[pos].idx.oid)) {
			*found = 1;
			return i;
		}

		i = (i + 1) & mask;
	}

	*found = 0;
	return i;
}

static inline uint32_t closest_pow2(uint32_t v)
{
	v = v - 1;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	return v + 1;
}

static void rehash_objects(struct packing_data *pdata)
{
	uint32_t i;
	struct object_entry *entry;

	pdata->index_size = closest_pow2(pdata->nr_objects * 3);
	if (pdata->index_size < 1024)
		pdata->index_size = 1024;

	free(pdata->index);
	CALLOC_ARRAY(pdata->index, pdata->index_size);

	entry = pdata->objects;

	for (i = 0; i < pdata->nr_objects; i++) {
		int found;
		uint32_t ix = locate_object_entry_hash(pdata,
						       &entry->idx.oid,
						       &found);

		if (found)
			BUG("Duplicate object in hash");

		pdata->index[ix] = i + 1;
		entry++;
	}
}

struct object_entry *packlist_find(struct packing_data *pdata,
				   const struct object_id *oid)
{
	uint32_t i;
	int found;

	if (!pdata->index_size)
		return NULL;

	i = locate_object_entry_hash(pdata, oid, &found);

	if (!found)
		return NULL;

	return &pdata->objects[pdata->index[i] - 1];
}

static void prepare_in_pack_by_idx(struct packing_data *pdata)
{
	struct packfile_store *packs = pdata->repo->objects->packfiles;
	struct packed_git **mapping, *p;
	int cnt = 0, nr = 1U << OE_IN_PACK_BITS;

	ALLOC_ARRAY(mapping, nr);
	/*
	 * oe_in_pack() on an all-zero'd object_entry
	 * (i.e. in_pack_idx also zero) should return NULL.
	 */
	mapping[cnt++] = NULL;
	for (p = packfile_store_get_all_packs(packs); p; p = p->next, cnt++) {
		if (cnt == nr) {
			free(mapping);
			return;
		}
		p->index = cnt;
		mapping[cnt] = p;
	}
	pdata->in_pack_by_idx = mapping;
}

/*
 * A new pack appears after prepare_in_pack_by_idx() has been
 * run. This is likely a race.
 *
 * We could map this new pack to in_pack_by_idx[] array, but then we
 * have to deal with full array anyway. And since it's hard to test
 * this fall back code, just stay simple and fall back to using
 * in_pack[] array.
 */
void oe_map_new_pack(struct packing_data *pack)
{
	uint32_t i;

	if (pack->in_pack)
		BUG("packing_data has already been converted to pack array");

	ALLOC_ARRAY(pack->in_pack, pack->nr_alloc);

	for (i = 0; i < pack->nr_objects; i++)
		pack->in_pack[i] = oe_in_pack(pack, pack->objects + i);

	FREE_AND_NULL(pack->in_pack_by_idx);
}

/* assume pdata is already zero'd by caller */
void prepare_packing_data(struct repository *r, struct packing_data *pdata)
{
	pdata->repo = r;

	if (git_env_bool("GIT_TEST_FULL_IN_PACK_ARRAY", 0)) {
		/*
		 * do not initialize in_pack_by_idx[] to force the
		 * slow path in oe_in_pack()
		 */
	} else {
		prepare_in_pack_by_idx(pdata);
	}

	pdata->oe_size_limit = git_env_ulong("GIT_TEST_OE_SIZE",
					     1U << OE_SIZE_BITS);
	pdata->oe_delta_size_limit = git_env_ulong("GIT_TEST_OE_DELTA_SIZE",
						   1UL << OE_DELTA_SIZE_BITS);
	init_recursive_mutex(&pdata->odb_lock);
}

void clear_packing_data(struct packing_data *pdata)
{
	if (!pdata)
		return;

	free(pdata->cruft_mtime);
	free(pdata->in_pack);
	free(pdata->in_pack_by_idx);
	free(pdata->in_pack_pos);
	free(pdata->index);
	free(pdata->layer);
	free(pdata->objects);
	free(pdata->tree_depth);
}

struct object_entry *packlist_alloc(struct packing_data *pdata,
				    const struct object_id *oid)
{
	struct object_entry *new_entry;

	if (pdata->nr_objects >= pdata->nr_alloc) {
		pdata->nr_alloc = (pdata->nr_alloc  + 1024) * 3 / 2;
		REALLOC_ARRAY(pdata->objects, pdata->nr_alloc);

		if (!pdata->in_pack_by_idx)
			REALLOC_ARRAY(pdata->in_pack, pdata->nr_alloc);
		if (pdata->delta_size)
			REALLOC_ARRAY(pdata->delta_size, pdata->nr_alloc);

		if (pdata->tree_depth)
			REALLOC_ARRAY(pdata->tree_depth, pdata->nr_alloc);

		if (pdata->layer)
			REALLOC_ARRAY(pdata->layer, pdata->nr_alloc);

		if (pdata->cruft_mtime)
			REALLOC_ARRAY(pdata->cruft_mtime, pdata->nr_alloc);
	}

	new_entry = pdata->objects + pdata->nr_objects++;

	memset(new_entry, 0, sizeof(*new_entry));
	oidcpy(&new_entry->idx.oid, oid);

	if (pdata->index_size * 3 <= pdata->nr_objects * 4)
		rehash_objects(pdata);
	else {
		int found;
		uint32_t pos = locate_object_entry_hash(pdata,
							&new_entry->idx.oid,
							&found);
		if (found)
			BUG("duplicate object inserted into hash");
		pdata->index[pos] = pdata->nr_objects;
	}

	if (pdata->in_pack)
		pdata->in_pack[pdata->nr_objects - 1] = NULL;

	if (pdata->tree_depth)
		pdata->tree_depth[pdata->nr_objects - 1] = 0;

	if (pdata->layer)
		pdata->layer[pdata->nr_objects - 1] = 0;

	if (pdata->cruft_mtime)
		pdata->cruft_mtime[pdata->nr_objects - 1] = 0;

	return new_entry;
}

void oe_set_delta_ext(struct packing_data *pdata,
		      struct object_entry *delta,
		      const struct object_id *oid)
{
	struct object_entry *base;

	ALLOC_GROW(pdata->ext_bases, pdata->nr_ext + 1, pdata->alloc_ext);
	base = &pdata->ext_bases[pdata->nr_ext++];
	memset(base, 0, sizeof(*base));
	oidcpy(&base->idx.oid, oid);

	/* These flags mark that we are not part of the actual pack output. */
	base->preferred_base = 1;
	base->filled = 1;

	delta->ext_base = 1;
	delta->delta_idx = base - pdata->ext_bases + 1;
}
