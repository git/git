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
	int ret;

	if (sz < 20)
		return error("corrupt link extension (too short)");
	si = init_split_index(istate);
	hashcpy(si->base_sha1, data);
	data += 20;
	sz -= 20;
	if (!sz)
		return 0;
	si->delete_bitmap = ewah_new();
	ret = ewah_read_mmap(si->delete_bitmap, data, sz);
	if (ret < 0)
		return error("corrupt delete bitmap in link extension");
	data += ret;
	sz -= ret;
	si->replace_bitmap = ewah_new();
	ret = ewah_read_mmap(si->replace_bitmap, data, sz);
	if (ret < 0)
		return error("corrupt replace bitmap in link extension");
	if (ret != sz)
		return error("garbage at the end of link extension");
	return 0;
}

int write_link_extension(struct strbuf *sb,
			 struct index_state *istate)
{
	struct split_index *si = istate->split_index;
	strbuf_add(sb, si->base_sha1, 20);
	if (!si->delete_bitmap && !si->replace_bitmap)
		return 0;
	ewah_serialize_strbuf(si->delete_bitmap, sb);
	ewah_serialize_strbuf(si->replace_bitmap, sb);
	return 0;
}

static void mark_base_index_entries(struct index_state *base)
{
	int i;
	/*
	 * To keep track of the shared entries between
	 * istate->base->cache[] and istate->cache[], base entry
	 * position is stored in each base entry. All positions start
	 * from 1 instead of 0, which is reserved to say "this is a new
	 * entry".
	 */
	for (i = 0; i < base->cache_nr; i++)
		base->cache[i]->index = i + 1;
}

void move_cache_to_base_index(struct index_state *istate)
{
	struct split_index *si = istate->split_index;
	int i;

	/*
	 * do not delete old si->base, its index entries may be shared
	 * with istate->cache[]. Accept a bit of leaking here because
	 * this code is only used by short-lived update-index.
	 */
	si->base = xcalloc(1, sizeof(*si->base));
	si->base->version = istate->version;
	/* zero timestamp disables racy test in ce_write_index() */
	si->base->timestamp = istate->timestamp;
	ALLOC_GROW(si->base->cache, istate->cache_nr, si->base->cache_alloc);
	si->base->cache_nr = istate->cache_nr;
	COPY_ARRAY(si->base->cache, istate->cache, istate->cache_nr);
	mark_base_index_entries(si->base);
	for (i = 0; i < si->base->cache_nr; i++)
		si->base->cache[i]->ce_flags &= ~CE_UPDATE_IN_BASE;
}

static void mark_entry_for_delete(size_t pos, void *data)
{
	struct index_state *istate = data;
	if (pos >= istate->cache_nr)
		die("position for delete %d exceeds base index size %d",
		    (int)pos, istate->cache_nr);
	istate->cache[pos]->ce_flags |= CE_REMOVE;
	istate->split_index->nr_deletions = 1;
}

static void replace_entry(size_t pos, void *data)
{
	struct index_state *istate = data;
	struct split_index *si = istate->split_index;
	struct cache_entry *dst, *src;

	if (pos >= istate->cache_nr)
		die("position for replacement %d exceeds base index size %d",
		    (int)pos, istate->cache_nr);
	if (si->nr_replacements >= si->saved_cache_nr)
		die("too many replacements (%d vs %d)",
		    si->nr_replacements, si->saved_cache_nr);
	dst = istate->cache[pos];
	if (dst->ce_flags & CE_REMOVE)
		die("entry %d is marked as both replaced and deleted",
		    (int)pos);
	src = si->saved_cache[si->nr_replacements];
	if (ce_namelen(src))
		die("corrupt link extension, entry %d should have "
		    "zero length name", (int)pos);
	src->index = pos + 1;
	src->ce_flags |= CE_UPDATE_IN_BASE;
	src->ce_namelen = dst->ce_namelen;
	copy_cache_entry(dst, src);
	free(src);
	si->nr_replacements++;
}

void merge_base_index(struct index_state *istate)
{
	struct split_index *si = istate->split_index;
	unsigned int i;

	mark_base_index_entries(si->base);

	si->saved_cache	    = istate->cache;
	si->saved_cache_nr  = istate->cache_nr;
	istate->cache_nr    = si->base->cache_nr;
	istate->cache	    = NULL;
	istate->cache_alloc = 0;
	ALLOC_GROW(istate->cache, istate->cache_nr, istate->cache_alloc);
	COPY_ARRAY(istate->cache, si->base->cache, istate->cache_nr);

	si->nr_deletions = 0;
	si->nr_replacements = 0;
	ewah_each_bit(si->replace_bitmap, replace_entry, istate);
	ewah_each_bit(si->delete_bitmap, mark_entry_for_delete, istate);
	if (si->nr_deletions)
		remove_marked_cache_entries(istate);

	for (i = si->nr_replacements; i < si->saved_cache_nr; i++) {
		if (!ce_namelen(si->saved_cache[i]))
			die("corrupt link extension, entry %d should "
			    "have non-zero length name", i);
		add_index_entry(istate, si->saved_cache[i],
				ADD_CACHE_OK_TO_ADD |
				ADD_CACHE_KEEP_CACHE_TREE |
				/*
				 * we may have to replay what
				 * merge-recursive.c:update_stages()
				 * does, which has this flag on
				 */
				ADD_CACHE_SKIP_DFCHECK);
		si->saved_cache[i] = NULL;
	}

	ewah_free(si->delete_bitmap);
	ewah_free(si->replace_bitmap);
	free(si->saved_cache);
	si->delete_bitmap  = NULL;
	si->replace_bitmap = NULL;
	si->saved_cache	   = NULL;
	si->saved_cache_nr = 0;
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
		 * that are not marked with either CE_MATCHED or
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
				ce->ce_flags |= CE_STRIP_NAME;
				ALLOC_GROW(entries, nr_entries+1, nr_alloc);
				entries[nr_entries++] = ce;
			}
		}
	}

	for (i = 0; i < istate->cache_nr; i++) {
		ce = istate->cache[i];
		if ((!si->base || !ce->index) && !(ce->ce_flags & CE_REMOVE)) {
			assert(!(ce->ce_flags & CE_STRIP_NAME));
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

void add_split_index(struct index_state *istate)
{
	if (!istate->split_index) {
		init_split_index(istate);
		istate->cache_changed |= SPLIT_INDEX_ORDERED;
	}
}

void remove_split_index(struct index_state *istate)
{
	if (istate->split_index) {
		/*
		 * can't discard_split_index(&the_index); because that
		 * will destroy split_index->base->cache[], which may
		 * be shared with the_index.cache[]. So yeah we're
		 * leaking a bit here.
		 */
		istate->split_index = NULL;
		istate->cache_changed |= SOMETHING_CHANGED;
	}
}
