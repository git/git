/* wrappers for the EVP API of OpenSSL 3+ */
#ifndef SHA256_OPENSSL_H
#define SHA256_OPENSSL_H
#include <openssl/evp.h>

struct openssl_SHA256_CTX {
	EVP_MD_CTX *ectx;
};

typedef struct openssl_SHA256_CTX openssl_SHA256_CTX;

static inline void openssl_SHA256_Init(struct openssl_SHA256_CTX *ctx)
{
	const EVP_MD *type = EVP_sha256();

	ctx->ectx = EVP_MD_CTX_new();
	if (!ctx->ectx)
		die("EVP_MD_CTX_new: out of memory");

	EVP_DigestInit_ex(ctx->ectx, type, NULL);
}

static inline void openssl_SHA256_Update(struct openssl_SHA256_CTX *ctx,
					const void *data,
					size_t len)
{
	EVP_DigestUpdate(ctx->ectx, data, len);
}

static inline void openssl_SHA256_Final(unsigned char *digest,
				       struct openssl_SHA256_CTX *ctx)
{
	EVP_DigestFinal_ex(ctx->ectx, digest, NULL);
	EVP_MD_CTX_free(ctx->ectx);
}

static inline void openssl_SHA256_Clone(struct openssl_SHA256_CTX *dst,
					const struct openssl_SHA256_CTX *src)
{
	EVP_MD_CTX_copy_ex(dst->ectx, src->ectx);
}

#define platform_SHA256_CTX openssl_SHA256_CTX
#define platform_SHA256_Init openssl_SHA256_Init
#define platform_SHA256_Clone openssl_SHA256_Clone
#define platform_SHA256_Update openssl_SHA256_Update
#define platform_SHA256_Final openssl_SHA256_Final

#endif /* SHA256_OPENSSL_H */
