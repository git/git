/*
 * SHA-1 implementation.
 *
 * Copyright (C) 2005 Paul Mackerras <paulus@samba.org>
 *
 * This version assumes we are running on a big-endian machine.
 * It calls an external sha1_core() to process blocks of 64 bytes.
 */
#include <stdio.h>
#include <string.h>
#include "sha1.h"

extern void ppc_sha1_core(uint32_t *hash, const unsigned char *p,
			  unsigned int nblocks);

int ppc_SHA1_Init(ppc_SHA_CTX *c)
{
	c->hash[0] = 0x67452301;
	c->hash[1] = 0xEFCDAB89;
	c->hash[2] = 0x98BADCFE;
	c->hash[3] = 0x10325476;
	c->hash[4] = 0xC3D2E1F0;
	c->len = 0;
	c->cnt = 0;
	return 0;
}

int ppc_SHA1_Update(ppc_SHA_CTX *c, const void *ptr, unsigned long n)
{
	unsigned long nb;
	const unsigned char *p = ptr;

	c->len += (uint64_t) n << 3;
	while (n != 0) {
		if (c->cnt || n < 64) {
			nb = 64 - c->cnt;
			if (nb > n)
				nb = n;
			memcpy(&c->buf.b[c->cnt], p, nb);
			if ((c->cnt += nb) == 64) {
				ppc_sha1_core(c->hash, c->buf.b, 1);
				c->cnt = 0;
			}
		} else {
			nb = n >> 6;
			ppc_sha1_core(c->hash, p, nb);
			nb <<= 6;
		}
		n -= nb;
		p += nb;
	}
	return 0;
}

int ppc_SHA1_Final(unsigned char *hash, ppc_SHA_CTX *c)
{
	unsigned int cnt = c->cnt;

	c->buf.b[cnt++] = 0x80;
	if (cnt > 56) {
		if (cnt < 64)
			memset(&c->buf.b[cnt], 0, 64 - cnt);
		ppc_sha1_core(c->hash, c->buf.b, 1);
		cnt = 0;
	}
	if (cnt < 56)
		memset(&c->buf.b[cnt], 0, 56 - cnt);
	c->buf.l[7] = c->len;
	ppc_sha1_core(c->hash, c->buf.b, 1);
	memcpy(hash, c->hash, 20);
	return 0;
}
