/* Plumbing with collition-detecting SHA1 code */

#ifdef DC_SHA1_EXTERNAL
#include <sha1dc/sha1.h>
#elif defined(DC_SHA1_SUBMODULE)
#include "sha1collisiondetection/lib/sha1.h"
#else
#include "sha1dc/sha1.h"
#endif

#ifdef DC_SHA1_EXTERNAL
void but_SHA1DCInit(SHA1_CTX *);
#else
#define but_SHA1DCInit	SHA1DCInit
#endif

void but_SHA1DCFinal(unsigned char [20], SHA1_CTX *);
void but_SHA1DCUpdate(SHA1_CTX *ctx, const void *data, unsigned long len);

#define platform_SHA_CTX SHA1_CTX
#define platform_SHA1_Init but_SHA1DCInit
#define platform_SHA1_Update but_SHA1DCUpdate
#define platform_SHA1_Final but_SHA1DCFinal
