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

/* Wild guess at the initial hash size */
#define INITIAL_HASH_SIZE 9
#define HASHBASE 65537 /* next_prime(2^16) */
/* We leave more room in smaller hash but do not let it
 * grow to have unused hole too much.
 */
#define INITIAL_FREE(sz_log2) ((1<<(sz_log2))*(sz_log2-3)/(sz_log2))

struct spanhash {
	unsigned long hashval;
	unsigned long cnt;
};
struct spanhash_top {
	int alloc_log2;
	int free;
	struct spanhash data[FLEX_ARRAY];
};

static struct spanhash *spanhash_find(struct spanhash_top *top,
				      unsigned long hashval)
{
	int sz = 1 << top->alloc_log2;
	int bucket = hashval & (sz - 1);
	while (1) {
		struct spanhash *h = &(top->data[bucket++]);
		if (!h->cnt)
			return NULL;
		if (h->hashval == hashval)
			return h;
		if (sz <= bucket)
			bucket = 0;
	}
}

static struct spanhash_top *spanhash_rehash(struct spanhash_top *orig)
{
	struct spanhash_top *new;
	int i;
	int osz = 1 << orig->alloc_log2;
	int sz = osz << 1;

	new = xmalloc(sizeof(*orig) + sizeof(struct spanhash) * sz);
	new->alloc_log2 = orig->alloc_log2 + 1;
	new->free = INITIAL_FREE(new->alloc_log2);
	memset(new->data, 0, sizeof(struct spanhash) * sz);
	for (i = 0; i < osz; i++) {
		struct spanhash *o = &(orig->data[i]);
		int bucket;
		if (!o->cnt)
			continue;
		bucket = o->hashval & (sz - 1);
		while (1) {
			struct spanhash *h = &(new->data[bucket++]);
			if (!h->cnt) {
				h->hashval = o->hashval;
				h->cnt = o->cnt;
				new->free--;
				break;
			}
			if (sz <= bucket)
				bucket = 0;
		}
	}
	free(orig);
	return new;
}

static struct spanhash_top *add_spanhash(struct spanhash_top *top,
					 unsigned long hashval)
{
	int bucket, lim;
	struct spanhash *h;

	lim = (1 << top->alloc_log2);
	bucket = hashval & (lim - 1);
	while (1) {
		h = &(top->data[bucket++]);
		if (!h->cnt) {
			h->hashval = hashval;
			h->cnt = 1;
			top->free--;
			if (top->free < 0)
				return spanhash_rehash(top);
			return top;
		}
		if (h->hashval == hashval) {
			h->cnt++;
			return top;
		}
		if (lim <= bucket)
			bucket = 0;
	}
}

static struct spanhash_top *hash_chars(unsigned char *buf, unsigned long sz)
{
	int i;
	unsigned long accum1, accum2, hashval;
	struct spanhash_top *hash;

	i = INITIAL_HASH_SIZE;
	hash = xmalloc(sizeof(*hash) + sizeof(struct spanhash) * (1<<i));
	hash->alloc_log2 = i;
	hash->free = INITIAL_FREE(i);
	memset(hash->data, 0, sizeof(struct spanhash) * (1<<i));

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
		hashval = (accum1 + accum2 * 0x61) % HASHBASE;
		hash = add_spanhash(hash, hashval);
		sz--;
	}
	return hash;
}

int diffcore_count_changes(void *src, unsigned long src_size,
			   void *dst, unsigned long dst_size,
			   void **src_count_p,
			   void **dst_count_p,
			   unsigned long delta_limit,
			   unsigned long *src_copied,
			   unsigned long *literal_added)
{
	int i, ssz;
	struct spanhash_top *src_count, *dst_count;
	unsigned long sc, la;

	if (src_size < 8 || dst_size < 8)
		return -1;

	src_count = dst_count = NULL;
	if (src_count_p)
		src_count = *src_count_p;
	if (!src_count) {
		src_count = hash_chars(src, src_size);
		if (src_count_p)
			*src_count_p = src_count;
	}
	if (dst_count_p)
		dst_count = *dst_count_p;
	if (!dst_count) {
		dst_count = hash_chars(dst, dst_size);
		if (dst_count_p)
			*dst_count_p = dst_count;
	}
	sc = la = 0;

	ssz = 1 << src_count->alloc_log2;
	for (i = 0; i < ssz; i++) {
		struct spanhash *s = &(src_count->data[i]);
		struct spanhash *d;
		unsigned dst_cnt, src_cnt;
		if (!s->cnt)
			continue;
		src_cnt = s->cnt;
		d = spanhash_find(dst_count, s->hashval);
		dst_cnt = d ? d->cnt : 0;
		if (src_cnt < dst_cnt) {
			la += dst_cnt - src_cnt;
			sc += src_cnt;
		}
		else
			sc += dst_cnt;
	}

	if (!src_count_p)
		free(src_count);
	if (!dst_count_p)
		free(dst_count);
	*src_copied = sc;
	*literal_added = la;
	return 0;
}
