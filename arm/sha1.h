/*
 * SHA-1 implementation optimized for ARM
 *
 * Copyright:	(C) 2005 by Nicolas Pitre <nico@cam.org>
 * Created:	September 17, 2005
 */

#include <stdint.h>

typedef struct sha_context {
	uint64_t len;
	uint32_t hash[5];
	unsigned char buffer[64];
} SHA_CTX;

void SHA1_Init(SHA_CTX *c);
void SHA1_Update(SHA_CTX *c, const void *p, unsigned long n);
void SHA1_Final(unsigned char *hash, SHA_CTX *c);
