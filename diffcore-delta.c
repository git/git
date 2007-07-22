#include "cache.h"
#include "diff.h"
#include "diffcore.h"

/*
 * Idea here is very simple.
 *
 * Almost all data we are interested in are text, but sometimes we have
 * to deal with binary data.  So we cut them into chunks delimited by
 * LF byte, or 64-byte sequence, whichever comes first, and hash them.
 *
 * For those chunks, if the source buffer has more instances of it
 * than the destination buffer, that means the difference are the
 * number of bytes not copied from source to destination.  If the
 * counts are the same, everything was copied from source to
 * destination.  If the destination has more, everything was copied,
 * and destination added more.
 *
 * We are doing an approximation so we do not really have to waste
 * memory by actually storing the sequence.  We just hash them into
 * somewhere around 2^16 hashbuckets and count the occurrences.
 */

/* Wild guess at the initial hash size */
#define INITIAL_HASH_SIZE 9

/* We leave more room in smaller hash but do not let it
 * grow to have unused hole too much.
 */
#define INITIAL_FREE(sz_log2) ((1<<(sz_log2))*(sz_log2-3)/(sz_log2))

/* A prime rather carefully chosen between 2^16..2^17, so that
 * HASHBASE < INITIAL_FREE(17).  We want to keep the maximum hashtable
 * size under the current 2<<17 maximum, which can hold this many
 * different values before overflowing to hashtable of size 2<<18.
 */
#define HASHBASE 107927

struct spanhash {
	unsigned int hashval;
	unsigned int cnt;
};
struct spanhash_top {
	int alloc_log2;
	int free;
	struct spanhash data[FLEX_ARRAY];
};

static struct spanhash *spanhash_find(struct spanhash_top *top,
				      unsigned int hashval)
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
					 unsigned int hashval, int cnt)
{
	int bucket, lim;
	struct spanhash *h;

	lim = (1 << top->alloc_log2);
	bucket = hashval & (lim - 1);
	while (1) {
		h = &(top->data[bucket++]);
		if (!h->cnt) {
			h->hashval = hashval;
			h->cnt = cnt;
			top->free--;
			if (top->free < 0)
				return spanhash_rehash(top);
			return top;
		}
		if (h->hashval == hashval) {
			h->cnt += cnt;
			return top;
		}
		if (lim <= bucket)
			bucket = 0;
	}
}

static struct spanhash_top *hash_chars(struct diff_filespec *one)
{
	int i, n;
	unsigned int accum1, accum2, hashval;
	struct spanhash_top *hash;
	unsigned char *buf = one->data;
	unsigned int sz = one->size;
	int is_text = !diff_filespec_is_binary(one);

	i = INITIAL_HASH_SIZE;
	hash = xmalloc(sizeof(*hash) + sizeof(struct spanhash) * (1<<i));
	hash->alloc_log2 = i;
	hash->free = INITIAL_FREE(i);
	memset(hash->data, 0, sizeof(struct spanhash) * (1<<i));

	n = 0;
	accum1 = accum2 = 0;
	while (sz) {
		unsigned int c = *buf++;
		unsigned int old_1 = accum1;
		sz--;

		/* Ignore CR in CRLF sequence if text */
		if (is_text && c == '\r' && sz && *buf == '\n')
			continue;

		accum1 = (accum1 << 7) ^ (accum2 >> 25);
		accum2 = (accum2 << 7) ^ (old_1 >> 25);
		accum1 += c;
		if (++n < 64 && c != '\n')
			continue;
		hashval = (accum1 + accum2 * 0x61) % HASHBASE;
		hash = add_spanhash(hash, hashval, n);
		n = 0;
		accum1 = accum2 = 0;
	}
	return hash;
}

int diffcore_count_changes(struct diff_filespec *src,
			   struct diff_filespec *dst,
			   void **src_count_p,
			   void **dst_count_p,
			   unsigned long delta_limit,
			   unsigned long *src_copied,
			   unsigned long *literal_added)
{
	int i, ssz;
	struct spanhash_top *src_count, *dst_count;
	unsigned long sc, la;

	src_count = dst_count = NULL;
	if (src_count_p)
		src_count = *src_count_p;
	if (!src_count) {
		src_count = hash_chars(src);
		if (src_count_p)
			*src_count_p = src_count;
	}
	if (dst_count_p)
		dst_count = *dst_count_p;
	if (!dst_count) {
		dst_count = hash_chars(dst);
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
