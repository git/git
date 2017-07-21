/*
 * This code is included at the end of sha1dc/sha1.h with the
 * SHA1DC_CUSTOM_TRAILING_INCLUDE_SHA1_H macro.
 */

/*
 * Same as SHA1DCFinal, but convert collision attack case into a verbose die().
 */
void git_SHA1DCFinal(unsigned char [20], SHA1_CTX *);

/*
 * Same as SHA1DCUpdate, but adjust types to match git's usual interface.
 */
void git_SHA1DCUpdate(SHA1_CTX *ctx, const void *data, unsigned long len);

#define platform_SHA_CTX SHA1_CTX
#define platform_SHA1_Init SHA1DCInit
#define platform_SHA1_Update git_SHA1DCUpdate
#define platform_SHA1_Final git_SHA1DCFinal
