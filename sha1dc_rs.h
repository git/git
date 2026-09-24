#ifndef SHA1DC_RS_H
#define SHA1DC_RS_H

#define platform_SHA_IS_SHA1DC /* used by "test-tool sha1-is-sha1dc" */

typedef struct sha1dc_rs_hasher *SHA1_CTX;

void sha1dc_rs_init(SHA1_CTX *);
void sha1dc_rs_clone(SHA1_CTX *, const SHA1_CTX *);
void sha1dc_rs_update(SHA1_CTX *, const void *, size_t);
void sha1dc_rs_final(unsigned char [20], SHA1_CTX *,
		     void (*die_fn)(const char *, ...));
void sha1dc_rs_discard(SHA1_CTX *);

#define platform_SHA_CTX SHA1_CTX
#define platform_SHA1_Init sha1dc_rs_init
#define platform_SHA1_Update sha1dc_rs_update
#define platform_SHA1_Final(hash, ctx) sha1dc_rs_final((hash), (ctx), die)
#define SHA1_NEEDS_CLONE_HELPER
#define platform_SHA1_Clone sha1dc_rs_clone
#define platform_SHA1_Discard sha1dc_rs_discard

#endif
