/* Plumbing with collition-detecting SHA1 code */

#ifdef DC_SHA1_EXTERNAL
#include <sha1dc/sha1.h>
#elif defined(DC_SHA1_SUBMODULE)
#include "sha1collisiondetection/lib/sha1.h"
#else
#include "sha1dc/sha1.h"
#endif

#ifdef DC_SHA1_EXTERNAL
void git_SHA1DCInit(SHA1_CTX *);
#else
#define git_SHA1DCInit	SHA1DCInit
#endif

void git_SHA1DCFinal(unsigned char [20], SHA1_CTX *,
		    void (*die_fn)(const char *, ...));
void git_SHA1DCUpdate(SHA1_CTX *ctx, const void *data, size_t len);

const char *sha1dc_backend_name(void);
void sha1dc_init(SHA1_CTX *);
void sha1dc_clone(SHA1_CTX *, const SHA1_CTX *);
void sha1dc_update(SHA1_CTX *, const void *, size_t);
void sha1dc_final(unsigned char [20], SHA1_CTX *,
		  void (*die_fn)(const char *, ...));
void sha1dc_discard(SHA1_CTX *);

#define platform_SHA_IS_SHA1DC /* used by "test-tool sha1-is-sha1dc" */

#ifndef platform_SHA_CTX
#define platform_SHA_CTX SHA1_CTX
#define platform_SHA1_Init sha1dc_init
#define platform_SHA1_Update sha1dc_update
#define platform_SHA1_Final(hash, ctx) sha1dc_final((hash), (ctx), die)
#endif
#define SHA1_NEEDS_CLONE_HELPER
#define platform_SHA1_Clone sha1dc_clone
#define platform_SHA1_Discard sha1dc_discard
