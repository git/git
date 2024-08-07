#ifndef HASH_LOOKUP_H
#define HASH_LOOKUP_H

typedef const struct object_id *oid_access_fn(size_t index, const void *table);

int oid_pos(const struct object_id *oid,
	    const void *table,
	    size_t nr,
	    oid_access_fn fn);

/*
 * Searches for hash in table, using the given fanout table to determine the
 * interval to search, then using binary search. Returns 1 if found, 0 if not.
 *
 * Takes the following parameters:
 *
 *  - hash: the hash to search for
 *  - fanout_nbo: a 256-element array of NETWORK-order 32-bit integers; the
 *    integer at position i represents the number of elements in table whose
 *    first byte is less than or equal to i
 *  - table: a sorted list of hashes with optional extra information in between
 *  - stride: distance between two consecutive elements in table (should be
 *    GIT_MAX_RAWSZ or greater)
 *  - result: if not NULL, this function stores the element index of the
 *    position found (if the search is successful) or the index of the least
 *    element that is greater than hash (if the search is not successful)
 *
 * This function does not verify the validity of the fanout table.
 */
int bsearch_hash(const unsigned char *hash, const uint32_t *fanout_nbo,
		 const unsigned char *table, size_t stride, uint32_t *result);
#endif
