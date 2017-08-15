/* Plumbing with collition-detecting SHA1 code */

#ifdef DC_SHA1_SUBMODULE
#include "sha1collisiondetection/lib/sha1.h"
#else
#include "sha1dc/sha1.h"
#endif

void git_SHA1DCFinal(unsigned char [20], SHA1_CTX *);
void git_SHA1DCUpdate(SHA1_CTX *ctx, const void *data, unsigned long len);

#define platform_SHA_CTX SHA1_CTX
#define platform_SHA1_Init SHA1DCInit
#define platform_SHA1_Update git_SHA1DCUpdate
#define platform_SHA1_Final git_SHA1DCFinal
