#define USE_THE_REPOSITORY_VARIABLE
#include "git-compat-util.h"
#include "hex.h"
#include "sha1dc_git.h"
#include "config.h"
#include "repository.h"
#include "thread-utils.h"

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
	if (!SHA1DCFinal(hash, ctx))
		return;
	die_fn("SHA-1 appears to be part of a collision attack: %s",
	       hash_to_hex_algop(hash, &hash_algos[GIT_HASH_SHA1]));
}

/*
 * Same as SHA1DCUpdate, but adjust types to match git's usual interface.
 */
void git_SHA1DCUpdate(SHA1_CTX *ctx, const void *vdata, size_t len)
{
	const char *data = vdata;
	while (len > INT_MAX) {
		SHA1DCUpdate(ctx, data, INT_MAX);
		data += INT_MAX;
		len -= INT_MAX;
	}
	SHA1DCUpdate(ctx, data, len);
}

static void sha1dc_c_clone(SHA1_CTX *dst, const SHA1_CTX *src)
{
	*dst = *src;
}

static void sha1dc_c_discard(SHA1_CTX *ctx UNUSED)
{
	/* The C context owns no resources. */
}

#ifdef USE_SHA1DC_ACCEL
static void sha1dc_accel_git_final(unsigned char hash[20], SHA1_CTX *ctx,
				  void (*die_fn)(const char *, ...))
{
	if (sha1dc_accel_final(hash, ctx))
		die_fn("SHA-1 appears to be part of a collision attack: %s",
		       hash_to_hex_algop(hash, &hash_algos[GIT_HASH_SHA1]));
}
#endif

struct sha1dc_backend {
	const char *name;
	void (*init)(SHA1_CTX *);
	void (*clone)(SHA1_CTX *, const SHA1_CTX *);
	void (*update)(SHA1_CTX *, const void *, size_t);
	void (*final)(unsigned char [20], SHA1_CTX *,
		      void (*die_fn)(const char *, ...));
	void (*discard)(SHA1_CTX *);
};

static const struct sha1dc_backend backends[] = {
	{ "c", git_SHA1DCInit, sha1dc_c_clone, git_SHA1DCUpdate,
	  git_SHA1DCFinal, sha1dc_c_discard },
#ifdef USE_SHA1DC_ACCEL
	{ "accel", git_SHA1DCInit, sha1dc_c_clone, sha1dc_accel_update,
	  sha1dc_accel_git_final, sha1dc_c_discard },
#endif
#ifdef DC_SHA1_RS
	{ "rust", sha1dc_rs_init, sha1dc_rs_clone, sha1dc_rs_update,
	  sha1dc_rs_final, sha1dc_rs_discard },
#endif
};

static const struct sha1dc_backend *selected;

static int sha1dc_config(const char *key, const char *value,
			 const struct config_context *ctx UNUSED, void *data)
{
	const struct sha1dc_backend **backend = data;
	size_t i;

	if (strcmp(key, "core.sha1dcbackend"))
		return 0;
	if (!value)
		return config_error_nonbool(key);
	for (i = 0; i < ARRAY_SIZE(backends); i++) {
		if (!strcasecmp(value, backends[i].name)) {
			*backend = &backends[i];
			return 0;
		}
	}
	die("unavailable or invalid SHA-1 backend: '%s'", value);
}

static void sha1dc_choose(void)
{
	const struct sha1dc_backend *backend = &backends[0];

	read_early_config(the_repository, sha1dc_config, &backend);
#ifdef DC_SHA1_RS
	if (backend->init == sha1dc_rs_init &&
	    !sha1dc_rs_ctx_fits(sizeof(SHA1_CTX), SHA1DC_CTX_ALIGNMENT))
		BUG("Rust SHA-1 context does not fit in SHA1_CTX");
#endif
#ifdef __GNUC__
	__atomic_store_n(&selected, backend, __ATOMIC_RELAXED);
#else
	selected = backend;
#endif
}

static pthread_once_t once = PTHREAD_ONCE_INIT;

static const struct sha1dc_backend *sha1dc_backend(void)
{
	int ret;
#ifdef __GNUC__
	const struct sha1dc_backend *backend =
		__atomic_load_n(&selected, __ATOMIC_RELAXED);

	if (backend)
		return backend;
#endif
	ret = pthread_once(&once, sha1dc_choose);
	if (ret)
		die("cannot initialize SHA-1 backend: %s", strerror(ret));
	return selected;
}

const char *sha1dc_backend_name(void)
{
	return sha1dc_backend()->name;
}

void sha1dc_init(SHA1_CTX *ctx)
{
	sha1dc_backend()->init(ctx);
}

void sha1dc_clone(SHA1_CTX *dst, const SHA1_CTX *src)
{
	sha1dc_backend()->clone(dst, src);
}

void sha1dc_update(SHA1_CTX *ctx, const void *buf, size_t len)
{
	sha1dc_backend()->update(ctx, buf, len);
}

void sha1dc_final(unsigned char hash[20], SHA1_CTX *ctx,
		  void (*die_fn)(const char *, ...))
{
	sha1dc_backend()->final(hash, ctx, die_fn);
}

void sha1dc_discard(SHA1_CTX *ctx)
{
	sha1dc_backend()->discard(ctx);
}
