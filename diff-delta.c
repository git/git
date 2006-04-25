/*
 * diff-delta.c: generate a delta between two buffers
 *
 *  Many parts of this file have been lifted from LibXDiff version 0.10.
 *  http://www.xmailserver.org/xdiff-lib.html
 *
 *  LibXDiff was written by Davide Libenzi <davidel@xmailserver.org>
 *  Copyright (C) 2003	Davide Libenzi
 *
 *  Many mods for GIT usage by Nicolas Pitre <nico@cam.org>, (C) 2005.
 *
 *  This file is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  Use of this within git automatically means that the LGPL
 *  licensing gets turned into GPLv2 within this project.
 */

#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "delta.h"


/* block size: min = 16, max = 64k, power of 2 */
#define BLK_SIZE 16

/* maximum hash entry list for the same hash bucket */
#define HASH_LIMIT 64

#define GR_PRIME 0x9e370001
#define HASH(v, shift) (((unsigned int)(v) * GR_PRIME) >> (shift))

struct index_entry {
	const unsigned char *ptr;
	unsigned int val;
	struct index_entry *next;
};

struct delta_index {
	const void *src_buf;
	unsigned long src_size;
	unsigned int hash_shift;
	struct index_entry *hash[0];
};

struct delta_index * create_delta_index(const void *buf, unsigned long bufsize)
{
	unsigned int i, hsize, hshift, entries, *hash_count;
	const unsigned char *data, *buffer = buf;
	struct delta_index *index;
	struct index_entry *entry, **hash;
	void *mem;

	if (!buf || !bufsize)
		return NULL;

	/* determine index hash size */
	entries = bufsize  / BLK_SIZE;
	hsize = entries / 4;
	for (i = 4; (1 << i) < hsize && i < 31; i++);
	hsize = 1 << i;
	hshift = 32 - i;

	/* allocate lookup index */
	mem = malloc(sizeof(*index) +
		     sizeof(*hash) * hsize +
		     sizeof(*entry) * entries);
	if (!mem)
		return NULL;
	index = mem;
	mem = index + 1;
	hash = mem;
	mem = hash + hsize;
	entry = mem;

	index->src_buf = buf;
	index->src_size = bufsize;
	index->hash_shift = hshift;
	memset(hash, 0, hsize * sizeof(*hash));

	/* allocate an array to count hash entries */
	hash_count = calloc(hsize, sizeof(*hash_count));
	if (!hash_count) {
		free(index);
		return NULL;
	}

	/* then populate the index */
	data = buffer + entries * BLK_SIZE - BLK_SIZE;
	while (data >= buffer) {
		unsigned int val = adler32(0, data, BLK_SIZE);
		i = HASH(val, hshift);
		entry->ptr = data;
		entry->val = val;
		entry->next = hash[i];
		hash[i] = entry++;
		hash_count[i]++;
		data -= BLK_SIZE;
 	}

	/*
	 * Determine a limit on the number of entries in the same hash
	 * bucket.  This guard us against patological data sets causing
	 * really bad hash distribution with most entries in the same hash
	 * bucket that would bring us to O(m*n) computing costs (m and n
	 * corresponding to reference and target buffer sizes).
	 *
	 * Make sure none of the hash buckets has more entries than
	 * we're willing to test.  Otherwise we cull the entry list
	 * uniformly to still preserve a good repartition across
	 * the reference buffer.
	 */
	for (i = 0; i < hsize; i++) {
		if (hash_count[i] < HASH_LIMIT)
			continue;
		entry = hash[i];
		do {
			struct index_entry *keep = entry;
			int skip = hash_count[i] / HASH_LIMIT / 2;
			do {
				entry = entry->next;
			} while(--skip && entry);
			keep->next = entry;
		} while(entry);
	}
	free(hash_count);

	return index;
}

void free_delta_index(struct delta_index *index)
{
	free(index);
}

/* provide the size of the copy opcode given the block offset and size */
#define COPYOP_SIZE(o, s) \
    (!!(o & 0xff) + !!(o & 0xff00) + !!(o & 0xff0000) + !!(o & 0xff000000) + \
     !!(s & 0xff) + !!(s & 0xff00) + 1)

/* the maximum size for any opcode */
#define MAX_OP_SIZE COPYOP_SIZE(0xffffffff, 0xffffffff)

void *
create_delta(const struct delta_index *index,
	     const void *trg_buf, unsigned long trg_size,
	     unsigned long *delta_size, unsigned long max_size)
{
	unsigned int i, outpos, outsize, hash_shift;
	int inscnt;
	const unsigned char *ref_data, *ref_top, *data, *top;
	unsigned char *out;

	if (!trg_buf || !trg_size)
		return NULL;

	outpos = 0;
	outsize = 8192;
	if (max_size && outsize >= max_size)
		outsize = max_size + MAX_OP_SIZE + 1;
	out = malloc(outsize);
	if (!out)
		return NULL;

	/* store reference buffer size */
	i = index->src_size;
	while (i >= 0x80) {
		out[outpos++] = i | 0x80;
		i >>= 7;
	}
	out[outpos++] = i;

	/* store target buffer size */
	i = trg_size;
	while (i >= 0x80) {
		out[outpos++] = i | 0x80;
		i >>= 7;
	}
	out[outpos++] = i;

	ref_data = index->src_buf;
	ref_top = ref_data + index->src_size;
	data = trg_buf;
	top = trg_buf + trg_size;
	hash_shift = index->hash_shift;
	inscnt = 0;

	while (data < top) {
		unsigned int moff = 0, msize = 0;
		struct index_entry *entry;
		unsigned int val = adler32(0, data, BLK_SIZE);
		i = HASH(val, hash_shift);
		for (entry = index->hash[i]; entry; entry = entry->next) {
			const unsigned char *ref = entry->ptr;
			const unsigned char *src = data;
			unsigned int ref_size = ref_top - ref;
			if (entry->val != val)
				continue;
			if (ref_size > top - src)
				ref_size = top - src;
			if (ref_size > 0x10000)
				ref_size = 0x10000;
			if (ref_size <= msize)
				break;
			while (ref_size-- && *src++ == *ref)
				ref++;
			if (msize < ref - entry->ptr) {
				/* this is our best match so far */
				msize = ref - entry->ptr;
				moff = entry->ptr - ref_data;
			}
		}

		if (!msize || msize < COPYOP_SIZE(moff, msize)) {
			if (!inscnt)
				outpos++;
			out[outpos++] = *data++;
			inscnt++;
			if (inscnt == 0x7f) {
				out[outpos - inscnt - 1] = inscnt;
				inscnt = 0;
			}
		} else {
			unsigned char *op;

			if (inscnt) {
				while (moff && ref_data[moff-1] == data[-1]) {
					if (msize == 0x10000)
						break;
					/* we can match one byte back */
					msize++;
					moff--;
					data--;
					outpos--;
					if (--inscnt)
						continue;
					outpos--;  /* remove count slot */
					inscnt--;  /* make it -1 */
					break;
				}
				out[outpos - inscnt - 1] = inscnt;
				inscnt = 0;
			}

			data += msize;
			op = out + outpos++;
			i = 0x80;

			if (moff & 0xff) { out[outpos++] = moff; i |= 0x01; }
			moff >>= 8;
			if (moff & 0xff) { out[outpos++] = moff; i |= 0x02; }
			moff >>= 8;
			if (moff & 0xff) { out[outpos++] = moff; i |= 0x04; }
			moff >>= 8;
			if (moff & 0xff) { out[outpos++] = moff; i |= 0x08; }

			if (msize & 0xff) { out[outpos++] = msize; i |= 0x10; }
			msize >>= 8;
			if (msize & 0xff) { out[outpos++] = msize; i |= 0x20; }

			*op = i;
		}

		if (outpos >= outsize - MAX_OP_SIZE) {
			void *tmp = out;
			outsize = outsize * 3 / 2;
			if (max_size && outsize >= max_size)
				outsize = max_size + MAX_OP_SIZE + 1;
			if (max_size && outpos > max_size)
				out = NULL;
			else
				out = realloc(out, outsize);
			if (!out) {
				free(tmp);
				return NULL;
			}
		}
	}

	if (inscnt)
		out[outpos - inscnt - 1] = inscnt;

	*delta_size = outpos;
	return out;
}
