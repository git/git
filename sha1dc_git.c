#ifdef DC_SHA1_RS
#define USE_THE_REPOSITORY_VARIABLE
#endif
#include "git-compat-util.h"
#include "hex.h"
#include "sha1dc_git.h"
#ifdef DC_SHA1_RS
#include "config.h"
#include "repository.h"
#endif

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
void git_SHA1DCFinal(unsigned char hash[20], SHA1_CTX *ctx,
		     void (*die_fn)(const char *, ...))
{
#ifdef USE_SHA1DC_ACCEL
	if (!sha1dc_accel_final(hash, ctx))
		return;
#else
	if (!SHA1DCFinal(hash, ctx))
		return;
#endif
	die_fn("SHA-1 appears to be part of a collision attack: %s",
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

#ifdef DC_SHA1_RS
static void sha1dc_c_clone(SHA1_CTX *dst, const SHA1_CTX *src)
{
	*dst = *src;
}

static void sha1dc_c_discard(SHA1_CTX *ctx UNUSED)
{
	/* The C context owns no resources. */
}

/* The first SHA-1 initialization must precede concurrent hashing. */
static void sha1dc_choose(SHA1_CTX *ctx);

void (*sha1dc_init)(SHA1_CTX *) = sha1dc_choose;
void (*sha1dc_clone)(SHA1_CTX *, const SHA1_CTX *);
void (*sha1dc_update)(SHA1_CTX *, const void *, size_t);
void (*sha1dc_final)(unsigned char [20], SHA1_CTX *,
		     void (*die_fn)(const char *, ...));
void (*sha1dc_discard)(SHA1_CTX *);

static void sha1dc_choose(SHA1_CTX *ctx)
{
	const char *backend;
	int use_c = 0;

	if (!repo_config_get_string_tmp(the_repository, "core.sha1dcbackend",
					&backend)) {
		if (!strcasecmp(backend, "c"))
			use_c = 1;
		else if (strcasecmp(backend, "rust"))
			die("invalid value for core.sha1dcBackend: '%s'",
			    backend);
	}

	sha1dc_clone = use_c ? sha1dc_c_clone : sha1dc_rs_clone;
	sha1dc_update = use_c ? git_SHA1DCUpdate : sha1dc_rs_update;
	sha1dc_final = use_c ? git_SHA1DCFinal : sha1dc_rs_final;
	sha1dc_discard = use_c ? sha1dc_c_discard : sha1dc_rs_discard;
	sha1dc_init = use_c ? git_SHA1DCInit : sha1dc_rs_init;
	sha1dc_init(ctx);
}
#endif
