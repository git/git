#ifndef OIDSET_H
#define OIDSET_H

#include "oidmap.h"

/**
 * This API is similar to sha1-array, in that it maintains a set of object ids
 * in a memory-efficient way. The major differences are:
 *
 *   1. It uses a hash, so we can do online duplicate removal, rather than
 *      sort-and-uniq at the end. This can reduce memory footprint if you have
 *      a large list of oids with many duplicates.
 *
 *   2. The per-unique-oid memory footprint is slightly higher due to hash
 *      table overhead.
 */

/**
 * A single oidset; should be zero-initialized (or use OIDSET_INIT).
 */
struct oidset {
	struct oidmap map;
};

#define OIDSET_INIT { OIDMAP_INIT }


static inline void oidset_init(struct oidset *set, size_t initial_size)
{
	return oidmap_init(&set->map, initial_size);
}

/**
 * Returns true iff `set` contains `oid`.
 */
int oidset_contains(const struct oidset *set, const struct object_id *oid);

/**
 * Insert the oid into the set; a copy is made, so "oid" does not need
 * to persist after this function is called.
 *
 * Returns 1 if the oid was already in the set, 0 otherwise. This can be used
 * to perform an efficient check-and-add.
 */
int oidset_insert(struct oidset *set, const struct object_id *oid);

/**
 * Remove the oid from the set.
 *
 * Returns 1 if the oid was present in the set, 0 otherwise.
 */
int oidset_remove(struct oidset *set, const struct object_id *oid);

/**
 * Remove all entries from the oidset, freeing any resources associated with
 * it.
 */
void oidset_clear(struct oidset *set);

struct oidset_iter {
	struct oidmap_iter m_iter;
};

static inline void oidset_iter_init(struct oidset *set,
				    struct oidset_iter *iter)
{
	oidmap_iter_init(&set->map, &iter->m_iter);
}

static inline struct object_id *oidset_iter_next(struct oidset_iter *iter)
{
	struct oidmap_entry *e = oidmap_iter_next(&iter->m_iter);
	return e ? &e->oid : NULL;
}

static inline struct object_id *oidset_iter_first(struct oidset *set,
						  struct oidset_iter *iter)
{
	oidset_iter_init(set, iter);
	return oidset_iter_next(iter);
}

#endif /* OIDSET_H */
