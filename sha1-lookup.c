#include "cache.h"
#include "sha1-lookup.h"

/*
 * Conventional binary search loop looks like this:
 *
 *	unsigned lo, hi;
 *      do {
 *              unsigned mi = (lo + hi) / 2;
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
 *   as lo, but never can be as same as hi), and check if it hits
 *   the target.  There are three cases:
 *
 *    - if it is a hit, we are happy.
 *
 *    - if it is strictly higher than the target, we set it to hi,
 *      and repeat the search.
 *
 *    - if it is strictly lower than the target, we update lo to
 *      one slot after it, because we allow lo to be at the target.
 *
 *   If the loop exits, there is no matching entry.
 *
 * When choosing 'mi', we do not have to take the "middle" but
 * anywhere in between lo and hi, as long as lo <= mi < hi is
 * satisfied.  When we somehow know that the distance between the
 * target and lo is much shorter than the target and hi, we could
 * pick mi that is much closer to lo than the midway.
 *
 * Now, we can take advantage of the fact that SHA-1 is a good hash
 * function, and as long as there are enough entries in the table, we
 * can expect uniform distribution.  An entry that begins with for
 * example "deadbeef..." is much likely to appear much later than in
 * the midway of the table.  It can reasonably be expected to be near
 * 87% (222/256) from the top of the table.
 *
 * The table at "table" holds at least "nr" entries of "elem_size"
 * bytes each.  Each entry has the SHA-1 key at "key_offset".  The
 * table is sorted by the SHA-1 key of the entries.  The caller wants
 * to find the entry with "key", and knows that the entry at "lo" is
 * not higher than the entry it is looking for, and that the entry at
 * "hi" is higher than the entry it is looking for.
 */
int sha1_entry_pos(const void *table,
		   size_t elem_size,
		   size_t key_offset,
		   unsigned lo, unsigned hi, unsigned nr,
		   const unsigned char *key)
{
	const unsigned char *base = table;
	const unsigned char *hi_key, *lo_key;
	unsigned ofs_0;
	static int debug_lookup = -1;

	if (debug_lookup < 0)
		debug_lookup = !!getenv("GIT_DEBUG_LOOKUP");

	if (!nr || lo >= hi)
		return -1;

	if (nr == hi)
		hi_key = NULL;
	else
		hi_key = base + elem_size * hi + key_offset;
	lo_key = base + elem_size * lo + key_offset;

	ofs_0 = 0;
	do {
		int cmp;
		unsigned ofs, mi, range;
		unsigned lov, hiv, kyv;
		const unsigned char *mi_key;

		range = hi - lo;
		if (hi_key) {
			for (ofs = ofs_0; ofs < 20; ofs++)
				if (lo_key[ofs] != hi_key[ofs])
					break;
			ofs_0 = ofs;
			/*
			 * byte 0 thru (ofs-1) are the same between
			 * lo and hi; ofs is the first byte that is
			 * different.
			 */
			hiv = hi_key[ofs_0];
			if (ofs_0 < 19)
				hiv = (hiv << 8) | hi_key[ofs_0+1];
		} else {
			hiv = 256;
			if (ofs_0 < 19)
				hiv <<= 8;
		}
		lov = lo_key[ofs_0];
		kyv = key[ofs_0];
		if (ofs_0 < 19) {
			lov = (lov << 8) | lo_key[ofs_0+1];
			kyv = (kyv << 8) | key[ofs_0+1];
		}
		assert(lov < hiv);

		if (kyv < lov)
			return -1 - lo;
		if (hiv < kyv)
			return -1 - hi;

		if (kyv == lov && lov < hiv - 1)
			kyv++;
		else if (kyv == hiv - 1 && lov < kyv)
			kyv--;

		mi = (range - 1) * (kyv - lov) / (hiv - lov) + lo;

		if (debug_lookup) {
			printf("lo %u hi %u rg %u mi %u ", lo, hi, range, mi);
			printf("ofs %u lov %x, hiv %x, kyv %x\n",
			       ofs_0, lov, hiv, kyv);
		}
		if (!(lo <= mi && mi < hi))
			die("assertion failure lo %u mi %u hi %u %s",
			    lo, mi, hi, sha1_to_hex(key));

		mi_key = base + elem_size * mi + key_offset;
		cmp = memcmp(mi_key + ofs_0, key + ofs_0, 20 - ofs_0);
		if (!cmp)
			return mi;
		if (cmp > 0) {
			hi = mi;
			hi_key = mi_key;
		}
		else {
			lo = mi + 1;
			lo_key = mi_key + elem_size;
		}
	} while (lo < hi);
	return -lo-1;
}
