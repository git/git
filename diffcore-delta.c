#include "cache.h"
#include "diff.h"
#include "diffcore.h"

/*
 * Idea here is very simple.
 *
 * We have total of (sz-N+1) N-byte overlapping sequences in buf whose
 * size is sz.  If the same N-byte sequence appears in both source and
 * destination, we say the byte that starts that sequence is shared
 * between them (i.e. copied from source to destination).
 *
 * For each possible N-byte sequence, if the source buffer has more
 * instances of it than the destination buffer, that means the
 * difference are the number of bytes not copied from source to
 * destination.  If the counts are the same, everything was copied
 * from source to destination.  If the destination has more,
 * everything was copied, and destination added more.
 *
 * We are doing an approximation so we do not really have to waste
 * memory by actually storing the sequence.  We just hash them into
 * somewhere around 2^16 hashbuckets and count the occurrences.
 *
 * The length of the sequence is arbitrarily set to 8 for now.
 */

#define HASHBASE 65537 /* next_prime(2^16) */

static void hash_chars(unsigned char *buf, unsigned long sz, int *count)
{
	unsigned int accum1, accum2, i;

	/* an 8-byte shift register made of accum1 and accum2.  New
	 * bytes come at LSB of accum2, and shifted up to accum1
	 */
	for (i = accum1 = accum2 = 0; i < 7; i++, sz--) {
		accum1 = (accum1 << 8) | (accum2 >> 24);
		accum2 = (accum2 << 8) | *buf++;
	}
	while (sz) {
		accum1 = (accum1 << 8) | (accum2 >> 24);
		accum2 = (accum2 << 8) | *buf++;
		/* We want something that hashes permuted byte
		 * sequences nicely; simpler hash like (accum1 ^
		 * accum2) does not perform as well.
		 */
		i = (accum1 + accum2 * 0x61) % HASHBASE;
		count[i]++;
		sz--;
	}
}

int diffcore_count_changes(void *src, unsigned long src_size,
			   void *dst, unsigned long dst_size,
			   unsigned long delta_limit,
			   unsigned long *src_copied,
			   unsigned long *literal_added)
{
	int *src_count, *dst_count, i;
	unsigned long sc, la;

	if (src_size < 8 || dst_size < 8)
		return -1;

	src_count = xcalloc(HASHBASE * 2, sizeof(int));
	dst_count = src_count + HASHBASE;
	hash_chars(src, src_size, src_count);
	hash_chars(dst, dst_size, dst_count);

	sc = la = 0;
	for (i = 0; i < HASHBASE; i++) {
		if (src_count[i] < dst_count[i]) {
			la += dst_count[i] - src_count[i];
			sc += src_count[i];
		}
		else /* i.e. if (dst_count[i] <= src_count[i]) */
			sc += dst_count[i];
	}
	*src_copied = sc;
	*literal_added = la;
	free(src_count);
	return 0;
}
