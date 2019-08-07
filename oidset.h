#ifndef OIDSET_H
#define OIDSET_H

#include "hashmap.h"
#include "khash.h"

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
	kh_oid_set_t set;
};

#define OIDSET_INIT { { 0 } }


/**
 * Initialize the oidset structure `set`.
 *
 * If `initial_size` is bigger than 0 then preallocate to allow inserting
 * the specified number of elements without further allocations.
 */
void oidset_init(struct oidset *set, size_t initial_size);

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

/**
 * Add the contents of the file 'path' to an initialized oidset.  Each line is
 * an unabbreviated object name.  Comments begin with '#', and trailing comments
 * are allowed.  Leading whitespace and empty or white-space only lines are
 * ignored.
 */
void oidset_parse_file(struct oidset *set, const char *path);

struct oidset_iter {
	kh_oid_set_t *set;
	khiter_t iter;
};

static inline void oidset_iter_init(struct oidset *set,
				    struct oidset_iter *iter)
{
	iter->set = &set->set;
	iter->iter = kh_begin(iter->set);
}

static inline struct object_id *oidset_iter_next(struct oidset_iter *iter)
{
	for (; iter->iter != kh_end(iter->set); iter->iter++) {
		if (kh_exist(iter->set, iter->iter))
			return &kh_key(iter->set, iter->iter++);
	}
	return NULL;
}

static inline struct object_id *oidset_iter_first(struct oidset *set,
						  struct oidset_iter *iter)
{
	oidset_iter_init(set, iter);
	return oidset_iter_next(iter);
}

#endif /* OIDSET_H */
