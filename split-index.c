#include "cache.h"
#include "split-index.h"

struct split_index *init_split_index(struct index_state *istate)
{
	if (!istate->split_index) {
		istate->split_index = xcalloc(1, sizeof(*istate->split_index));
		istate->split_index->refcount = 1;
	}
	return istate->split_index;
}

int read_link_extension(struct index_state *istate,
			 const void *data_, unsigned long sz)
{
	const unsigned char *data = data_;
	struct split_index *si;
	if (sz < 20)
		return error("corrupt link extension (too short)");
	si = init_split_index(istate);
	hashcpy(si->base_sha1, data);
	data += 20;
	sz -= 20;
	if (sz)
		return error("garbage at the end of link extension");
	return 0;
}

int write_link_extension(struct strbuf *sb,
			 struct index_state *istate)
{
	struct split_index *si = istate->split_index;
	strbuf_add(sb, si->base_sha1, 20);
	return 0;
}

static void mark_base_index_entries(struct index_state *base)
{
	int i;
	/*
	 * To keep track of the shared entries between
	 * istate->base->cache[] and istate->cache[], base entry
	 * position is stored in each base entry. All positions start
	 * from 1 instead of 0, which is resrved to say "this is a new
	 * entry".
	 */
	for (i = 0; i < base->cache_nr; i++)
		base->cache[i]->index = i + 1;
}

void merge_base_index(struct index_state *istate)
{
	struct split_index *si = istate->split_index;

	mark_base_index_entries(si->base);
	istate->cache_nr = si->base->cache_nr;
	ALLOC_GROW(istate->cache, istate->cache_nr, istate->cache_alloc);
	memcpy(istate->cache, si->base->cache,
	       sizeof(*istate->cache) * istate->cache_nr);
}

void prepare_to_write_split_index(struct index_state *istate)
{
	struct split_index *si = init_split_index(istate);
	/* take cache[] out temporarily */
	si->saved_cache_nr = istate->cache_nr;
	istate->cache_nr = 0;
}

void finish_writing_split_index(struct index_state *istate)
{
	struct split_index *si = init_split_index(istate);
	istate->cache_nr = si->saved_cache_nr;
}

void discard_split_index(struct index_state *istate)
{
	struct split_index *si = istate->split_index;
	if (!si)
		return;
	istate->split_index = NULL;
	si->refcount--;
	if (si->refcount)
		return;
	if (si->base) {
		discard_index(si->base);
		free(si->base);
	}
	free(si);
}

void save_or_free_index_entry(struct index_state *istate, struct cache_entry *ce)
{
	if (ce->index &&
	    istate->split_index &&
	    istate->split_index->base &&
	    ce->index <= istate->split_index->base->cache_nr &&
	    ce == istate->split_index->base->cache[ce->index - 1])
		ce->ce_flags |= CE_REMOVE;
	else
		free(ce);
}
