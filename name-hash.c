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

static void hash_index_entry_directories(struct index_state *istate, struct cache_entry *ce)
{
	/*
	 * Throw each directory component in the hash for quick lookup
	 * during a git status. Directory components are stored with their
	 * closing slash.  Despite submodules being a directory, they never
	 * reach this point, because they are stored without a closing slash
	 * in the cache.
	 *
	 * Note that the cache_entry stored with the directory does not
	 * represent the directory itself.  It is a pointer to an existing
	 * filename, and its only purpose is to represent existence of the
	 * directory in the cache.  It is very possible multiple directory
	 * hash entries may point to the same cache_entry.
	 */
	unsigned int hash;
	void **pos;

	const char *ptr = ce->name;
	while (*ptr) {
		while (*ptr && *ptr != '/')
			++ptr;
		if (*ptr == '/') {
			++ptr;
			hash = hash_name(ce->name, ptr - ce->name);
			pos = insert_hash(hash, ce, &istate->name_hash);
			if (pos) {
				ce->dir_next = *pos;
				*pos = ce;
			}
		}
	}
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

	if (ignore_case)
		hash_index_entry_directories(istate, ce);
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

	if (!icase)
		return 0;

	/*
	 * If the entry we're comparing is a filename (no trailing slash), then compare
	 * the lengths exactly.
	 */
	if (name[namelen - 1] != '/')
		return slow_same_name(name, namelen, ce->name, len);

	/*
	 * For a directory, we point to an arbitrary cache_entry filename.  Just
	 * make sure the directory portion matches.
	 */
	return slow_same_name(name, namelen, ce->name, namelen < len ? namelen : len);
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
		if (icase && name[namelen - 1] == '/')
			ce = ce->dir_next;
		else
			ce = ce->next;
	}

	/*
	 * Might be a submodule.  Despite submodules being directories,
	 * they are stored in the name hash without a closing slash.
	 * When ignore_case is 1, directories are stored in the name hash
	 * with their closing slash.
	 *
	 * The side effect of this storage technique is we have need to
	 * remove the slash from name and perform the lookup again without
	 * the slash.  If a match is made, S_ISGITLINK(ce->mode) will be
	 * true.
	 */
	if (icase && name[namelen - 1] == '/') {
		ce = index_name_exists(istate, name, namelen - 1, icase);
		if (ce && S_ISGITLINK(ce->ce_mode))
			return ce;
	}
	return NULL;
}
