/*
 * name-hash.c
 *
 * Hashing names in the index state
 *
 * Copyright (C) 2008 Linus Torvalds
 */
#define NO_THE_INDEX_COMPATIBILITY_MACROS
#include "cache.h"

/*
 * This removes bit 5 if bit 6 is set.
 *
 * That will make US-ASCII characters hash to their upper-case
 * equivalent. We could easily do this one whole word at a time,
 * but that's for future worries.
 */
static inline unsigned char icase_hash(unsigned char c)
{
	return c & ~((c & 0x40) >> 1);
}

static unsigned int hash_name(const char *name, int namelen)
{
	unsigned int hash = 0x123;

	do {
		unsigned char c = *name++;
		c = icase_hash(c);
		hash = hash*101 + c;
	} while (--namelen);
	return hash;
}

static void hash_index_entry(struct index_state *istate, struct cache_entry *ce)
{
	void **pos;
	unsigned int hash;

	if (ce->ce_flags & CE_HASHED)
		return;
	ce->ce_flags |= CE_HASHED;
	ce->next = NULL;
	hash = hash_name(ce->name, ce_namelen(ce));
	pos = insert_hash(hash, ce, &istate->name_hash);
	if (pos) {
		ce->next = *pos;
		*pos = ce;
	}
}

static void lazy_init_name_hash(struct index_state *istate)
{
	int nr;

	if (istate->name_hash_initialized)
		return;
	for (nr = 0; nr < istate->cache_nr; nr++)
		hash_index_entry(istate, istate->cache[nr]);
	istate->name_hash_initialized = 1;
}

void add_name_hash(struct index_state *istate, struct cache_entry *ce)
{
	ce->ce_flags &= ~CE_UNHASHED;
	if (istate->name_hash_initialized)
		hash_index_entry(istate, ce);
}

static int slow_same_name(const char *name1, int len1, const char *name2, int len2)
{
	if (len1 != len2)
		return 0;

	while (len1) {
		unsigned char c1 = *name1++;
		unsigned char c2 = *name2++;
		len1--;
		if (c1 != c2) {
			c1 = toupper(c1);
			c2 = toupper(c2);
			if (c1 != c2)
				return 0;
		}
	}
	return 1;
}

static int same_name(const struct cache_entry *ce, const char *name, int namelen, int icase)
{
	int len = ce_namelen(ce);

	/*
	 * Always do exact compare, even if we want a case-ignoring comparison;
	 * we do the quick exact one first, because it will be the common case.
	 */
	if (len == namelen && !cache_name_compare(name, namelen, ce->name, len))
		return 1;

	return icase && slow_same_name(name, namelen, ce->name, len);
}

struct cache_entry *index_name_exists(struct index_state *istate, const char *name, int namelen, int icase)
{
	unsigned int hash = hash_name(name, namelen);
	struct cache_entry *ce;

	lazy_init_name_hash(istate);
	ce = lookup_hash(hash, &istate->name_hash);

	while (ce) {
		if (!(ce->ce_flags & CE_UNHASHED)) {
			if (same_name(ce, name, namelen, icase))
				return ce;
		}
		ce = ce->next;
	}
	return NULL;
}
