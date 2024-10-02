/* wrappers for the EVP API of OpenSSL 3+ */
#ifndef SHA1_OPENSSL_H
#define SHA1_OPENSSL_H
#include <openssl/evp.h>

struct openssl_SHA1_CTX {
	EVP_MD_CTX *ectx;
};

typedef struct openssl_SHA1_CTX openssl_SHA1_CTX;

static inline void openssl_SHA1_Init(struct openssl_SHA1_CTX *ctx)
{
	const EVP_MD *type = EVP_sha1();

	ctx->ectx = EVP_MD_CTX_new();
	if (!ctx->ectx)
		die("EVP_MD_CTX_new: out of memory");

	EVP_DigestInit_ex(ctx->ectx, type, NULL);
}

static inline void openssl_SHA1_Update(struct openssl_SHA1_CTX *ctx,
					const void *data,
					size_t len)
{
	EVP_DigestUpdate(ctx->ectx, data, len);
}

static inline void openssl_SHA1_Final(unsigned char *digest,
				       struct openssl_SHA1_CTX *ctx)
{
	EVP_DigestFinal_ex(ctx->ectx, digest, NULL);
	EVP_MD_CTX_free(ctx->ectx);
}

static inline void openssl_SHA1_Clone(struct openssl_SHA1_CTX *dst,
					const struct openssl_SHA1_CTX *src)
{
	EVP_MD_CTX_copy_ex(dst->ectx, src->ectx);
}

#ifndef platform_SHA_CTX
#define platform_SHA_CTX openssl_SHA1_CTX
#define platform_SHA1_Init openssl_SHA1_Init
#define platform_SHA1_Clone openssl_SHA1_Clone
#define platform_SHA1_Update openssl_SHA1_Update
#define platform_SHA1_Final openssl_SHA1_Final
#endif

#endif /* SHA1_OPENSSL_H */
