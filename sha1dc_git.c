#include "cache.h"

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
