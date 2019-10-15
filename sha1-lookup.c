#include "cache.h"
#include "sha1-lookup.h"

static uint32_t take2(const unsigned char *sha1)
{
	return ((sha1[0] << 8) | sha1[1]);
}

/*
 * Conventional binary search loop looks like this:
 *
 *      do {
 *              int mi = lo + (hi - lo) / 2;
 *              int cmp = "entry pointed at by mi" minus "target";
 *              if (!cmp)
 *                      return (mi is the wanted one)
 *              if (cmp > 0)
 *                      hi = mi; "mi is larger than target"
 *              else
 *                      lo = mi+1; "mi is smaller than target"
 *      } while (lo < hi);
 *
 * The invariants are:
 *
 * - When entering the loop, lo points at a slot that is never
 *   above the target (it could be at the target), hi points at a
 *   slot that is guaranteed to be above the target (it can never
 *   be at the target).
 *
 * - We find a point 'mi' between lo and hi (mi could be the same
 *   as lo, but never can be the same as hi), and check if it hits
 *   the target.  There are three cases:
 *
 *    - if it is a hit, we are happy.
 *
 *    - if it is strictly higher than the target, we update hi with
 *      it.
 *
 *    - if it is strictly lower than the target, we update lo to be
 *      one slot after it, because we allow lo to be at the target.
 *
 * When choosing 'mi', we do not have to take the "middle" but
 * anywhere in between lo and hi, as long as lo <= mi < hi is
 * satisfied.  When we somehow know that the distance between the
 * target and lo is much shorter than the target and hi, we could
 * pick mi that is much closer to lo than the midway.
 */
/*
 * The table should contain "nr" elements.
 * The sha1 of element i (between 0 and nr - 1) should be returned
 * by "fn(i, table)".
 */
int sha1_pos(const unsigned char *hash, void *table, size_t nr,
	     sha1_access_fn fn)
{
	size_t hi = nr;
	size_t lo = 0;
	size_t mi = 0;

	if (!nr)
		return -1;

	if (nr != 1) {
		size_t lov, hiv, miv, ofs;

		for (ofs = 0; ofs < the_hash_algo->rawsz - 2; ofs += 2) {
			lov = take2(fn(0, table) + ofs);
			hiv = take2(fn(nr - 1, table) + ofs);
			miv = take2(hash + ofs);
			if (miv < lov)
				return -1;
			if (hiv < miv)
				return index_pos_to_insert_pos(nr);
			if (lov != hiv) {
				/*
				 * At this point miv could be equal
				 * to hiv (but sha1 could still be higher);
				 * the invariant of (mi < hi) should be
				 * kept.
				 */
				mi = (nr - 1) * (miv - lov) / (hiv - lov);
				if (lo <= mi && mi < hi)
					break;
				BUG("assertion failed in binary search");
			}
		}
	}

	do {
		int cmp;
		cmp = hashcmp(fn(mi, table), hash);
		if (!cmp)
			return mi;
		if (cmp > 0)
			hi = mi;
		else
			lo = mi + 1;
		mi = lo + (hi - lo) / 2;
	} while (lo < hi);
	return index_pos_to_insert_pos(lo);
}

int bsearch_hash(const unsigned char *sha1, const uint32_t *fanout_nbo,
		 const unsigned char *table, size_t stride, uint32_t *result)
{
	uint32_t hi, lo;

	hi = ntohl(fanout_nbo[*sha1]);
	lo = ((*sha1 == 0x0) ? 0 : ntohl(fanout_nbo[*sha1 - 1]));

	while (lo < hi) {
		unsigned mi = lo + (hi - lo) / 2;
		int cmp = hashcmp(table + mi * stride, sha1);

		if (!cmp) {
			if (result)
				*result = mi;
			return 1;
		}
		if (cmp > 0)
			hi = mi;
		else
			lo = mi + 1;
	}

	if (result)
		*result = lo;
	return 0;
}
