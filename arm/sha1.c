/*
 * SHA-1 implementation optimized for ARM
 *
 * Copyright:   (C) 2005 by Nicolas Pitre <nico@cam.org>
 * Created:     September 17, 2005
 */

#include <string.h>
#include "sha1.h"

extern void arm_sha_transform(uint32_t *hash, const unsigned char *data, uint32_t *W);

void arm_SHA1_Init(arm_SHA_CTX *c)
{
	c->len = 0;
	c->hash[0] = 0x67452301;
	c->hash[1] = 0xefcdab89;
	c->hash[2] = 0x98badcfe;
	c->hash[3] = 0x10325476;
	c->hash[4] = 0xc3d2e1f0;
}

void arm_SHA1_Update(arm_SHA_CTX *c, const void *p, unsigned long n)
{
	uint32_t workspace[80];
	unsigned int partial;
	unsigned long done;

	partial = c->len & 0x3f;
	c->len += n;
	if ((partial + n) >= 64) {
		if (partial) {
			done = 64 - partial;
			memcpy(c->buffer + partial, p, done);
			arm_sha_transform(c->hash, c->buffer, workspace);
			partial = 0;
		} else
			done = 0;
		while (n >= done + 64) {
			arm_sha_transform(c->hash, p + done, workspace);
			done += 64;
		}
	} else
		done = 0;
	if (n - done)
		memcpy(c->buffer + partial, p + done, n - done);
}

void arm_SHA1_Final(unsigned char *hash, arm_SHA_CTX *c)
{
	uint64_t bitlen;
	uint32_t bitlen_hi, bitlen_lo;
	unsigned int i, offset, padlen;
	unsigned char bits[8];
	static const unsigned char padding[64] = { 0x80, };

	bitlen = c->len << 3;
	offset = c->len & 0x3f;
	padlen = ((offset < 56) ? 56 : (64 + 56)) - offset;
	arm_SHA1_Update(c, padding, padlen);

	bitlen_hi = bitlen >> 32;
	bitlen_lo = bitlen & 0xffffffff;
	bits[0] = bitlen_hi >> 24;
	bits[1] = bitlen_hi >> 16;
	bits[2] = bitlen_hi >> 8;
	bits[3] = bitlen_hi;
	bits[4] = bitlen_lo >> 24;
	bits[5] = bitlen_lo >> 16;
	bits[6] = bitlen_lo >> 8;
	bits[7] = bitlen_lo;
	arm_SHA1_Update(c, bits, 8);

	for (i = 0; i < 5; i++) {
		uint32_t v = c->hash[i];
		hash[0] = v >> 24;
		hash[1] = v >> 16;
		hash[2] = v >> 8;
		hash[3] = v;
		hash += 4;
	}
}
