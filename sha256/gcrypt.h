#ifndef SHA256_GCRYPT_H
#define SHA256_GCRYPT_H

#include <gcrypt.h>

#define SHA256_DIGEST_SIZE 32

typedef gcry_md_hd_t gcrypt_SHA256_CTX;

inline void gcrypt_SHA256_Init(gcrypt_SHA256_CTX *ctx)
{
	gcry_md_open(ctx, GCRY_MD_SHA256, 0);
}

inline void gcrypt_SHA256_Update(gcrypt_SHA256_CTX *ctx, const void *data, size_t len)
{
	gcry_md_write(*ctx, data, len);
}

inline void gcrypt_SHA256_Final(unsigned char *digest, gcrypt_SHA256_CTX *ctx)
{
	memcpy(digest, gcry_md_read(*ctx, GCRY_MD_SHA256), SHA256_DIGEST_SIZE);
}

#define platform_SHA256_CTX gcrypt_SHA256_CTX
#define platform_SHA256_Init gcrypt_SHA256_Init
#define platform_SHA256_Update gcrypt_SHA256_Update
#define platform_SHA256_Final gcrypt_SHA256_Final

#endif
