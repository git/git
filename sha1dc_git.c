#include "cache.h"

#ifdef DC_SHA1_EXTERNAL
/*
 * Same as SHA1DCInit, but with default save_hash=0
 */
void git_SHA1DCInit(SHA1_CTX *ctx)
{
	SHA1DCInit(ctx);
	SHA1DCSetSafeHash(ctx, 0);
}
#endif

/*
 * Same as SHA1DCFinal, but convert collision attack case into a verbose die().
 */
void git_SHA1DCFinal(unsigned char hash[20], SHA1_CTX *ctx)
{
	if (!SHA1DCFinal(hash, ctx))
		return;
	die("SHA-1 appears to be part of a collision attack: %s",
	    sha1_to_hex(hash));
}

/*
 * Same as SHA1DCUpdate, but adjust types to match git's usual interface.
 */
void git_SHA1DCUpdate(SHA1_CTX *ctx, const void *vdata, unsigned long len)
{
	const char *data = vdata;
	/* We expect an unsigned long, but sha1dc only takes an int */
	while (len > INT_MAX) {
		SHA1DCUpdate(ctx, data, INT_MAX);
		data += INT_MAX;
		len -= INT_MAX;
	}
	SHA1DCUpdate(ctx, data, len);
}
