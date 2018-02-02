#ifndef SHA1_LOOKUP_H
#define SHA1_LOOKUP_H

typedef const unsigned char *sha1_access_fn(size_t index, void *table);

extern int sha1_pos(const unsigned char *sha1,
		    void *table,
		    size_t nr,
		    sha1_access_fn fn);

/*
 * Searches for sha1 in table, using the given fanout table to determine the
 * interval to search, then using binary search. Returns the element index of
 * the position found if successful, -i-1 if not (where i is the index of the
 * least element that is greater than sha1).
 *
 * Takes the following parameters:
 *
 *  - sha1: the hash to search for
 *  - fanout: a 256-element array of NETWORK-order 32-bit integers; the integer
 *    at position i represents the number of elements in table whose first byte
 *    is less than or equal to i
 *  - table: a sorted list of hashes with optional extra information in between
 *  - stride: distance between two consecutive elements in table (should be
 *    GIT_MAX_RAWSZ or greater)
 *
 * This function does not verify the validity of the fanout table.
 */
extern int bsearch_hash(const unsigned char *sha1, const void *fanout,
			const void *table, size_t stride);
#endif
