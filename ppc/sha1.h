/*
 * SHA-1 implementation.
 *
 * Copyright (C) 2005 Paul Mackerras <paulus@samba.org>
 */
#include <stdint.h>

typedef struct sha_context {
	uint32_t hash[5];
	uint32_t cnt;
	uint64_t len;
	union {
		unsigned char b[64];
		uint64_t l[8];
	} buf;
} SHA_CTX;

int SHA1_Init(SHA_CTX *c);
int SHA1_Update(SHA_CTX *c, const void *p, unsigned long n);
int SHA1_Final(unsigned char *hash, SHA_CTX *c);
