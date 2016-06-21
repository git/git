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
#include "delta.h"


/* block size: min = 16, max = 64k, power of 2 */
#define BLK_SIZE 16

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define GR_PRIME 0x9e370001
#define HASH(v, b) (((unsigned int)(v) * GR_PRIME) >> (32 - (b)))
	
/* largest prime smaller than 65536 */
#define BASE 65521

/* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */
#define NMAX 5552

#define DO1(buf, i)  { s1 += buf[i]; s2 += s1; }
#define DO2(buf, i)  DO1(buf, i); DO1(buf, i + 1);
#define DO4(buf, i)  DO2(buf, i); DO2(buf, i + 2);
#define DO8(buf, i)  DO4(buf, i); DO4(buf, i + 4);
#define DO16(buf)    DO8(buf, 0); DO8(buf, 8);

static unsigned int adler32(unsigned int adler, const unsigned char *buf, int len)
{
	int k;
	unsigned int s1 = adler & 0xffff;
	unsigned int s2 = adler >> 16;

	while (len > 0) {
		k = MIN(len, NMAX);
		len -= k;
		while (k >= 16) {
			DO16(buf);
			buf += 16;
			k -= 16;
		}
		if (k != 0)
			do {
				s1 += *buf++;
				s2 += s1;
			} while (--k);
		s1 %= BASE;
		s2 %= BASE;
	}

	return (s2 << 16) | s1;
}

static unsigned int hashbits(unsigned int size)
{
	unsigned int val = 1, bits = 0;
	while (val < size && bits < 32) {
		val <<= 1;
	       	bits++;
	}
	return bits ? bits: 1;
}

typedef struct s_chanode {
	struct s_chanode *next;
	int icurr;
} chanode_t;

typedef struct s_chastore {
	chanode_t *head, *tail;
	int isize, nsize;
	chanode_t *ancur;
	chanode_t *sncur;
	int scurr;
} chastore_t;

static void cha_init(chastore_t *cha, int isize, int icount)
{
	cha->head = cha->tail = NULL;
	cha->isize = isize;
	cha->nsize = icount * isize;
	cha->ancur = cha->sncur = NULL;
	cha->scurr = 0;
}

static void *cha_alloc(chastore_t *cha)
{
	chanode_t *ancur;
	void *data;

	ancur = cha->ancur;
	if (!ancur || ancur->icurr == cha->nsize) {
		ancur = malloc(sizeof(chanode_t) + cha->nsize);
		if (!ancur)
			return NULL;
		ancur->icurr = 0;
		ancur->next = NULL;
		if (cha->tail)
			cha->tail->next = ancur;
		if (!cha->head)
			cha->head = ancur;
		cha->tail = ancur;
		cha->ancur = ancur;
	}

	data = (void *)ancur + sizeof(chanode_t) + ancur->icurr;
	ancur->icurr += cha->isize;
	return data;
}

static void cha_free(chastore_t *cha)
{
	chanode_t *cur = cha->head;
	while (cur) {
		chanode_t *tmp = cur;
		cur = cur->next;
		free(tmp);
	}
}

typedef struct s_bdrecord {
	struct s_bdrecord *next;
	unsigned int fp;
	const unsigned char *ptr;
} bdrecord_t;

typedef struct s_bdfile {
	const unsigned char *data, *top;
	chastore_t cha;
	unsigned int fphbits;
	bdrecord_t **fphash;
} bdfile_t;

static int delta_prepare(const unsigned char *buf, int bufsize, bdfile_t *bdf)
{
	unsigned int fphbits;
	int i, hsize;
	const unsigned char *base, *data, *top;
	bdrecord_t *brec;
	bdrecord_t **fphash;

	fphbits = hashbits(bufsize / BLK_SIZE + 1);
	hsize = 1 << fphbits;
	fphash = malloc(hsize * sizeof(bdrecord_t *));
	if (!fphash)
		return -1;
	for (i = 0; i < hsize; i++)
		fphash[i] = NULL;
	cha_init(&bdf->cha, sizeof(bdrecord_t), hsize / 4 + 1);

	bdf->data = data = base = buf;
	bdf->top = top = buf + bufsize;
	data += (bufsize / BLK_SIZE) * BLK_SIZE;
	if (data == top)
		data -= BLK_SIZE;

	for ( ; data >= base; data -= BLK_SIZE) {
		brec = cha_alloc(&bdf->cha);
		if (!brec) {
			cha_free(&bdf->cha);
			free(fphash);
			return -1;
		}
		brec->fp = adler32(0, data, MIN(BLK_SIZE, top - data));
		brec->ptr = data;
		i = HASH(brec->fp, fphbits);
		brec->next = fphash[i];
		fphash[i] = brec;
	}

	bdf->fphbits = fphbits;
	bdf->fphash = fphash;

	return 0;
}

static void delta_cleanup(bdfile_t *bdf)
{
	free(bdf->fphash);
	cha_free(&bdf->cha);
}

#define COPYOP_SIZE(o, s) \
    (!!(o & 0xff) + !!(o & 0xff00) + !!(o & 0xff0000) + !!(o & 0xff000000) + \
     !!(s & 0xff) + !!(s & 0xff00) + 1)

void *diff_delta(void *from_buf, unsigned long from_size,
		 void *to_buf, unsigned long to_size,
		 unsigned long *delta_size,
		 unsigned long max_size)
{
	int i, outpos, outsize, inscnt, csize, msize, moff;
	unsigned int fp;
	const unsigned char *data, *top, *ptr1, *ptr2;
	unsigned char *out, *orig;
	bdrecord_t *brec;
	bdfile_t bdf;

	if (!from_size || !to_size || delta_prepare(from_buf, from_size, &bdf))
		return NULL;
	
	outpos = 0;
	outsize = 8192;
	out = malloc(outsize);
	if (!out) {
		delta_cleanup(&bdf);
		return NULL;
	}

	data = to_buf;
	top = to_buf + to_size;

	/* store reference buffer size */
	out[outpos++] = from_size;
	from_size >>= 7;
	while (from_size) {
		out[outpos - 1] |= 0x80;
		out[outpos++] = from_size;
		from_size >>= 7;
	}

	/* store target buffer size */
	out[outpos++] = to_size;
	to_size >>= 7;
	while (to_size) {
		out[outpos - 1] |= 0x80;
		out[outpos++] = to_size;
		to_size >>= 7;
	}

	inscnt = 0;
	moff = 0;
	while (data < top) {
		msize = 0;
		fp = adler32(0, data, MIN(top - data, BLK_SIZE));
		i = HASH(fp, bdf.fphbits);
		for (brec = bdf.fphash[i]; brec; brec = brec->next) {
			if (brec->fp == fp) {
				csize = bdf.top - brec->ptr;
				if (csize > top - data)
					csize = top - data;
				for (ptr1 = brec->ptr, ptr2 = data; 
				     csize && *ptr1 == *ptr2;
				     csize--, ptr1++, ptr2++);

				csize = ptr1 - brec->ptr;
				if (csize > msize) {
					moff = brec->ptr - bdf.data;
					msize = csize;
					if (msize >= 0x10000) {
						msize = 0x10000;
						break;
					}
				}
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
			if (inscnt) {
				out[outpos - inscnt - 1] = inscnt;
				inscnt = 0;
			}

			data += msize;
			orig = out + outpos++;
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

			*orig = i;
		}

		if (max_size && outpos > max_size) {
			free(out);
			delta_cleanup(&bdf);
			return NULL;
		}

		/* next time around the largest possible output is 1 + 4 + 3 */
		if (outpos > outsize - 8) {
			void *tmp = out;
			outsize = outsize * 3 / 2;
			out = realloc(out, outsize);
			if (!out) {
				free(tmp);
				delta_cleanup(&bdf);
				return NULL;
			}
		}
	}

	if (inscnt)
		out[outpos - inscnt - 1] = inscnt;

	delta_cleanup(&bdf);
	*delta_size = outpos;
	return out;
}
