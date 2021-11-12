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

void git_SHA1DCFinal(unsigned char [20], SHA1_CTX *);
void git_SHA1DCUpdate(SHA1_CTX *ctx, const void *data, size_t len);

#define platform_SHA_IS_SHA1DC /* used by "test-tool sha1-is-sha1dc" */
#define platform_SHA_CTX SHA1_CTX
#define platform_SHA1_Init git_SHA1DCInit
#define platform_SHA1_Update git_SHA1DCUpdate
#define platform_SHA1_Final git_SHA1DCFinal
