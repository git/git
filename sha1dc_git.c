#include "git-compat-util.h"
#include "sha1dc_git.h"
#include "hex.h"

/*
 * Unless told otherwise, hash with sha1dc-accel/, which gives the same
 * results as sha1dc/ on the same SHA1_CTX.
 */
#if !defined(DC_SHA1_EXTERNAL) && !defined(DC_SHA1_NO_ACCEL)
#include "sha1dc-accel/sha1.h"
#define USE_SHA1DC_ACCEL
#endif

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
#ifdef USE_SHA1DC_ACCEL
	if (!sha1dc_accel_final(hash, ctx))
		return;
#else
	if (!SHA1DCFinal(hash, ctx))
		return;
#endif
	die("SHA-1 appears to be part of a collision attack: %s",
	    hash_to_hex_algop(hash, &hash_algos[GIT_HASH_SHA1]));
}

/*
 * Same as SHA1DCUpdate, but adjust types to match git's usual interface.
 */
void git_SHA1DCUpdate(SHA1_CTX *ctx, const void *vdata, size_t len)
{
#ifdef USE_SHA1DC_ACCEL
	sha1dc_accel_update(ctx, vdata, len);
#else
	const char *data = vdata;
	while (len > INT_MAX) {
		SHA1DCUpdate(ctx, data, INT_MAX);
		data += INT_MAX;
		len -= INT_MAX;
	}
	SHA1DCUpdate(ctx, data, len);
#endif
}
