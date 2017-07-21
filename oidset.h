#ifndef OIDSET_H
#define OIDSET_H

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
	struct hashmap map;
};

#define OIDSET_INIT { { NULL } }

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
 * Remove all entries from the oidset, freeing any resources associated with
 * it.
 */
void oidset_clear(struct oidset *set);

#endif /* OIDSET_H */
