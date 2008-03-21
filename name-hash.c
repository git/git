/*
 * name-hash.c
 *
 * Hashing names in the index state
 *
 * Copyright (C) 2008 Linus Torvalds
 */
#define NO_THE_INDEX_COMPATIBILITY_MACROS
#include "cache.h"

static unsigned int hash_name(const char *name, int namelen)
{
	unsigned int hash = 0x123;

	do {
		unsigned char c = *name++;
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

int index_name_exists(struct index_state *istate, const char *name, int namelen)
{
	unsigned int hash = hash_name(name, namelen);
	struct cache_entry *ce;

	lazy_init_name_hash(istate);
	ce = lookup_hash(hash, &istate->name_hash);

	while (ce) {
		if (!(ce->ce_flags & CE_UNHASHED)) {
			if (!cache_name_compare(name, namelen, ce->name, ce->ce_flags))
				return 1;
		}
		ce = ce->next;
	}
	return 0;
}
