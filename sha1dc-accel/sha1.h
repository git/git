#ifndef SHA1DC_ACCEL_SHA1_H
#define SHA1DC_ACCEL_SHA1_H

#if defined(DC_SHA1_SUBMODULE)
#include "../sha1collisiondetection/lib/sha1.h"
#else
#include "../sha1dc/sha1.h"
#endif

/*
 * A faster implementation of SHA-1 with collision detection, working on
 * the SHA1_CTX of sha1dc/ (or of the sha1collisiondetection submodule).
 * SHA1DCInit() and the SHA1DCSet*() functions set it up as usual; these
 * then take the place of SHA1DCUpdate() and SHA1DCFinal(), with the same
 * results. See sha1.c.
 */

void sha1dc_accel_update(SHA1_CTX *ctx, const void *data, size_t len);
int sha1dc_accel_final(unsigned char hash[20], SHA1_CTX *ctx);

/*
 * The implementation in use, such as "shani+avx2" or "portable". For tests,
 * sha1dc_accel_select() switches to another one by that name; it returns -1
 * if this build or CPU does not have it. Neither is thread-safe.
 */
const char *sha1dc_accel_backend(void);
int sha1dc_accel_select(const char *name);
/* The names this build and CPU can use, NULL-terminated. */
const char *const *sha1dc_accel_backends(void);

#endif /* SHA1DC_ACCEL_SHA1_H */
