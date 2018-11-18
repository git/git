#include "cache.h"
#include "object.h"
#include "pack.h"
#include "pack-objects.h"
#include "packfile.h"
#include "config.h"

static uint32_t locate_object_entry_hash(struct packing_data *pdata,
					 const unsigned char *sha1,
					 int *found)
{
	uint32_t i, mask = (pdata->index_size - 1);

	i = sha1hash(sha1) & mask;

	while (pdata->index[i] > 0) {
		uint32_t pos = pdata->index[i] - 1;

		if (hasheq(sha1, pdata->objects[pos].idx.oid.hash)) {
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
	pdata->index = xcalloc(pdata->index_size, sizeof(*pdata->index));

	entry = pdata->objects;

	for (i = 0; i < pdata->nr_objects; i++) {
		int found;
		uint32_t ix = locate_object_entry_hash(pdata,
						       entry->idx.oid.hash,
						       &found);

		if (found)
			BUG("Duplicate object in hash");

		pdata->index[ix] = i + 1;
		entry++;
	}
}

struct object_entry *packlist_find(struct packing_data *pdata,
				   const unsigned char *sha1,
				   uint32_t *index_pos)
{
	uint32_t i;
	int found;

	if (!pdata->index_size)
		return NULL;

	i = locate_object_entry_hash(pdata, sha1, &found);

	if (index_pos)
		*index_pos = i;

	if (!found)
		return NULL;

	return &pdata->objects[pdata->index[i] - 1];
}

static void prepare_in_pack_by_idx(struct packing_data *pdata)
{
	struct packed_git **mapping, *p;
	int cnt = 0, nr = 1U << OE_IN_PACK_BITS;

	ALLOC_ARRAY(mapping, nr);
	/*
	 * oe_in_pack() on an all-zero'd object_entry
	 * (i.e. in_pack_idx also zero) should return NULL.
	 */
	mapping[cnt++] = NULL;
	for (p = get_all_packs(pdata->repo); p; p = p->next, cnt++) {
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
void oe_map_new_pack(struct packing_data *pack,
		     struct packed_git *p)
{
	uint32_t i;

	REALLOC_ARRAY(pack->in_pack, pack->nr_alloc);

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
#ifndef NO_PTHREADS
	pthread_mutex_init(&pdata->lock, NULL);
#endif
}

struct object_entry *packlist_alloc(struct packing_data *pdata,
				    const unsigned char *sha1,
				    uint32_t index_pos)
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
	}

	new_entry = pdata->objects + pdata->nr_objects++;

	memset(new_entry, 0, sizeof(*new_entry));
	hashcpy(new_entry->idx.oid.hash, sha1);

	if (pdata->index_size * 3 <= pdata->nr_objects * 4)
		rehash_objects(pdata);
	else
		pdata->index[index_pos] = pdata->nr_objects;

	if (pdata->in_pack)
		pdata->in_pack[pdata->nr_objects - 1] = NULL;

	if (pdata->tree_depth)
		pdata->tree_depth[pdata->nr_objects - 1] = 0;

	if (pdata->layer)
		pdata->layer[pdata->nr_objects - 1] = 0;

	return new_entry;
}

void oe_set_delta_ext(struct packing_data *pdata,
		      struct object_entry *delta,
		      const unsigned char *sha1)
{
	struct object_entry *base;

	ALLOC_GROW(pdata->ext_bases, pdata->nr_ext + 1, pdata->alloc_ext);
	base = &pdata->ext_bases[pdata->nr_ext++];
	memset(base, 0, sizeof(*base));
	hashcpy(base->idx.oid.hash, sha1);

	/* These flags mark that we are not part of the actual pack output. */
	base->preferred_base = 1;
	base->filled = 1;

	delta->ext_base = 1;
	delta->delta_idx = base - pdata->ext_bases + 1;
}
