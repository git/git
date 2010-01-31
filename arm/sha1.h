/*
 * SHA-1 implementation optimized for ARM
 *
 * Copyright:	(C) 2005 by Nicolas Pitre <nico@cam.org>
 * Created:	September 17, 2005
 */

#include <stdint.h>

typedef struct {
	uint64_t len;
	uint32_t hash[5];
	unsigned char buffer[64];
} arm_SHA_CTX;

void arm_SHA1_Init(arm_SHA_CTX *c);
void arm_SHA1_Update(arm_SHA_CTX *c, const void *p, unsigned long n);
void arm_SHA1_Final(unsigned char *hash, arm_SHA_CTX *c);

#define git_SHA_CTX	arm_SHA_CTX
#define git_SHA1_Init	arm_SHA1_Init
#define git_SHA1_Update	arm_SHA1_Update
#define git_SHA1_Final	arm_SHA1_Final
