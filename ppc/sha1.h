/*
 * SHA-1 implementation.
 *
 * Copyright (C) 2005 Paul Mackerras <paulus@samba.org>
 */
#include <stdint.h>

typedef struct {
	uint32_t hash[5];
	uint32_t cnt;
	uint64_t len;
	union {
		unsigned char b[64];
		uint64_t l[8];
	} buf;
} ppc_SHA_CTX;

int ppc_SHA1_Init(ppc_SHA_CTX *c);
int ppc_SHA1_Update(ppc_SHA_CTX *c, const void *p, unsigned long n);
int ppc_SHA1_Final(unsigned char *hash, ppc_SHA_CTX *c);

#define git_SHA_CTX	ppc_SHA_CTX
#define git_SHA1_Init	ppc_SHA1_Init
#define git_SHA1_Update	ppc_SHA1_Update
#define git_SHA1_Final	ppc_SHA1_Final
