#ifndef SHA1DC_RS_H
#define SHA1DC_RS_H

#define platform_SHA_CTX union sha1dc_ctx
#include "sha1dc_git.h"

typedef struct sha1dc_rs_hasher *sha1dc_rs_ctx;

union sha1dc_ctx {
	SHA1_CTX c;
	sha1dc_rs_ctx rs;
};

void sha1dc_rs_init(SHA1_CTX *);
void sha1dc_rs_clone(SHA1_CTX *, const SHA1_CTX *);
void sha1dc_rs_update(SHA1_CTX *, const void *, size_t);
void sha1dc_rs_final(unsigned char [20], SHA1_CTX *,
		     void (*die_fn)(const char *, ...));
void sha1dc_rs_discard(SHA1_CTX *);

extern void (*sha1dc_init)(SHA1_CTX *);
extern void (*sha1dc_clone)(SHA1_CTX *, const SHA1_CTX *);
extern void (*sha1dc_update)(SHA1_CTX *, const void *, size_t);
extern void (*sha1dc_final)(unsigned char [20], SHA1_CTX *,
			    void (*die_fn)(const char *, ...));
extern void (*sha1dc_discard)(SHA1_CTX *);

#define platform_SHA1_Init(ctx) sha1dc_init(&(ctx)->c)
#define platform_SHA1_Update(ctx, data, len) \
	sha1dc_update(&(ctx)->c, (data), (len))
#define platform_SHA1_Final(hash, ctx) \
	sha1dc_final((hash), &(ctx)->c, die)
#define SHA1_NEEDS_CLONE_HELPER
#define platform_SHA1_Clone(dst, src) sha1dc_clone(&(dst)->c, &(src)->c)
#define platform_SHA1_Discard(ctx) sha1dc_discard(&(ctx)->c)

#endif
