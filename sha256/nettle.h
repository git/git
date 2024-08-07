#ifndef SHA256_NETTLE_H
#define SHA256_NETTLE_H

#include <nettle/sha2.h>

typedef struct sha256_ctx nettle_SHA256_CTX;

static inline void nettle_SHA256_Init(nettle_SHA256_CTX *ctx)
{
	sha256_init(ctx);
}

static inline void nettle_SHA256_Update(nettle_SHA256_CTX *ctx,
					const void *data,
					size_t len)
{
	sha256_update(ctx, len, data);
}

static inline void nettle_SHA256_Final(unsigned char *digest,
				       nettle_SHA256_CTX *ctx)
{
	sha256_digest(ctx, SHA256_DIGEST_SIZE, digest);
}

#define platform_SHA256_CTX nettle_SHA256_CTX
#define platform_SHA256_Init nettle_SHA256_Init
#define platform_SHA256_Update nettle_SHA256_Update
#define platform_SHA256_Final nettle_SHA256_Final

#endif
