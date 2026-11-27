#ifndef SHA1DC_RS_H
#define SHA1DC_RS_H

#include "sha1dc_git.h"

/*
 * Like mem-pool.c, query alignment in C99 using a one-byte prefix.
 * The compiler pads before "aligned" as required by SHA1_CTX; its offset
 * gives the alignment without allocating a probe object. This avoids
 * compiler-specific operators until C11's _Alignof is the baseline.
 */
struct sha1dc_ctx_alignment {
	char unalign;
	SHA1_CTX aligned;
};
#define SHA1DC_CTX_ALIGNMENT offsetof(struct sha1dc_ctx_alignment, aligned)

int sha1dc_rs_ctx_fits(size_t, size_t);
void sha1dc_rs_init(SHA1_CTX *);
void sha1dc_rs_clone(SHA1_CTX *, const SHA1_CTX *);
void sha1dc_rs_update(SHA1_CTX *, const void *, size_t);
void sha1dc_rs_final(unsigned char [20], SHA1_CTX *,
		     void (*die_fn)(const char *, ...));
void sha1dc_rs_discard(SHA1_CTX *);

#endif
