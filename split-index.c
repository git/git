#include "cache.h"
#include "split-index.h"
#include "ewah/ewok.h"

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

static int write_strbuf(void *user_data, const void *data, size_t len)
{
	struct strbuf *sb = user_data;
	strbuf_add(sb, data, len);
	return len;
}

int write_link_extension(struct strbuf *sb,
			 struct index_state *istate)
{
	struct split_index *si = istate->split_index;
	strbuf_add(sb, si->base_sha1, 20);
	if (!si->delete_bitmap && !si->replace_bitmap)
		return 0;
	ewah_serialize_to(si->delete_bitmap, write_strbuf, sb);
	ewah_serialize_to(si->replace_bitmap, write_strbuf, sb);
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
	struct cache_entry **entries = NULL, *ce;
	int i, nr_entries = 0, nr_alloc = 0;

	si->delete_bitmap = ewah_new();
	si->replace_bitmap = ewah_new();

	if (si->base) {
		/* Go through istate->cache[] and mark CE_MATCHED to
		 * entry with positive index. We'll go through
		 * base->cache[] later to delete all entries in base
		 * that are not marked eith either CE_MATCHED or
		 * CE_UPDATE_IN_BASE. If istate->cache[i] is a
		 * duplicate, deduplicate it.
		 */
		for (i = 0; i < istate->cache_nr; i++) {
			struct cache_entry *base;
			/* namelen is checked separately */
			const unsigned int ondisk_flags =
				CE_STAGEMASK | CE_VALID | CE_EXTENDED_FLAGS;
			unsigned int ce_flags, base_flags, ret;
			ce = istate->cache[i];
			if (!ce->index)
				continue;
			if (ce->index > si->base->cache_nr) {
				ce->index = 0;
				continue;
			}
			ce->ce_flags |= CE_MATCHED; /* or "shared" */
			base = si->base->cache[ce->index - 1];
			if (ce == base)
				continue;
			if (ce->ce_namelen != base->ce_namelen ||
			    strcmp(ce->name, base->name)) {
				ce->index = 0;
				continue;
			}
			ce_flags = ce->ce_flags;
			base_flags = base->ce_flags;
			/* only on-disk flags matter */
			ce->ce_flags   &= ondisk_flags;
			base->ce_flags &= ondisk_flags;
			ret = memcmp(&ce->ce_stat_data, &base->ce_stat_data,
				     offsetof(struct cache_entry, name) -
				     offsetof(struct cache_entry, ce_stat_data));
			ce->ce_flags = ce_flags;
			base->ce_flags = base_flags;
			if (ret)
				ce->ce_flags |= CE_UPDATE_IN_BASE;
			free(base);
			si->base->cache[ce->index - 1] = ce;
		}
		for (i = 0; i < si->base->cache_nr; i++) {
			ce = si->base->cache[i];
			if ((ce->ce_flags & CE_REMOVE) ||
			    !(ce->ce_flags & CE_MATCHED))
				ewah_set(si->delete_bitmap, i);
			else if (ce->ce_flags & CE_UPDATE_IN_BASE) {
				ewah_set(si->replace_bitmap, i);
				ALLOC_GROW(entries, nr_entries+1, nr_alloc);
				entries[nr_entries++] = ce;
			}
		}
	}

	for (i = 0; i < istate->cache_nr; i++) {
		ce = istate->cache[i];
		if ((!si->base || !ce->index) && !(ce->ce_flags & CE_REMOVE)) {
			ALLOC_GROW(entries, nr_entries+1, nr_alloc);
			entries[nr_entries++] = ce;
		}
		ce->ce_flags &= ~CE_MATCHED;
	}

	/*
	 * take cache[] out temporarily, put entries[] in its place
	 * for writing
	 */
	si->saved_cache = istate->cache;
	si->saved_cache_nr = istate->cache_nr;
	istate->cache = entries;
	istate->cache_nr = nr_entries;
}

void finish_writing_split_index(struct index_state *istate)
{
	struct split_index *si = init_split_index(istate);

	ewah_free(si->delete_bitmap);
	ewah_free(si->replace_bitmap);
	si->delete_bitmap = NULL;
	si->replace_bitmap = NULL;
	free(istate->cache);
	istate->cache = si->saved_cache;
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

void replace_index_entry_in_base(struct index_state *istate,
				 struct cache_entry *old,
				 struct cache_entry *new)
{
	if (old->index &&
	    istate->split_index &&
	    istate->split_index->base &&
	    old->index <= istate->split_index->base->cache_nr) {
		new->index = old->index;
		if (old != istate->split_index->base->cache[new->index - 1])
			free(istate->split_index->base->cache[new->index - 1]);
		istate->split_index->base->cache[new->index - 1] = new;
	}
}
