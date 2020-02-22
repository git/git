#ifndef HASH_H
#define HASH_H

#include "git-compat-util.h"

#if defined(SHA1_PPC)
#include "ppc/sha1.h"
#elif defined(SHA1_APPLE)
#include <CommonCrypto/CommonDigest.h>
#elif defined(SHA1_OPENSSL)
#include <openssl/sha.h>
#elif defined(SHA1_DC)
#include "sha1dc_git.h"
#else /* SHA1_BLK */
#include "block-sha1/sha1.h"
#endif

#if defined(SHA256_GCRYPT)
#define SHA256_NEEDS_CLONE_HELPER
#include "sha256/gcrypt.h"
#elif defined(SHA256_OPENSSL)
#include <openssl/sha.h>
#else
#include "sha256/block/sha256.h"
#endif

#ifndef platform_SHA_CTX
/*
 * platform's underlying implementation of SHA-1; could be OpenSSL,
 * blk_SHA, Apple CommonCrypto, etc...  Note that the relevant
 * SHA-1 header may have already defined platform_SHA_CTX for our
 * own implementations like block-sha1 and ppc-sha1, so we list
 * the default for OpenSSL compatible SHA-1 implementations here.
 */
#define platform_SHA_CTX	SHA_CTX
#define platform_SHA1_Init	SHA1_Init
#define platform_SHA1_Update	SHA1_Update
#define platform_SHA1_Final    	SHA1_Final
#endif

#define git_SHA_CTX		platform_SHA_CTX
#define git_SHA1_Init		platform_SHA1_Init
#define git_SHA1_Update		platform_SHA1_Update
#define git_SHA1_Final		platform_SHA1_Final

#ifndef platform_SHA256_CTX
#define platform_SHA256_CTX	SHA256_CTX
#define platform_SHA256_Init	SHA256_Init
#define platform_SHA256_Update	SHA256_Update
#define platform_SHA256_Final	SHA256_Final
#endif

#define git_SHA256_CTX		platform_SHA256_CTX
#define git_SHA256_Init		platform_SHA256_Init
#define git_SHA256_Update	platform_SHA256_Update
#define git_SHA256_Final	platform_SHA256_Final

#ifdef platform_SHA256_Clone
#define git_SHA256_Clone	platform_SHA256_Clone
#endif

#ifdef SHA1_MAX_BLOCK_SIZE
#include "compat/sha1-chunked.h"
#undef git_SHA1_Update
#define git_SHA1_Update		git_SHA1_Update_Chunked
#endif

static inline void git_SHA1_Clone(git_SHA_CTX *dst, const git_SHA_CTX *src)
{
	memcpy(dst, src, sizeof(*dst));
}

#ifndef SHA256_NEEDS_CLONE_HELPER
static inline void git_SHA256_Clone(git_SHA256_CTX *dst, const git_SHA256_CTX *src)
{
	memcpy(dst, src, sizeof(*dst));
}
#endif

/*
 * Note that these constants are suitable for indexing the hash_algos array and
 * comparing against each other, but are otherwise arbitrary, so they should not
 * be exposed to the user or serialized to disk.  To know whether a
 * git_hash_algo struct points to some usable hash function, test the format_id
 * field for being non-zero.  Use the name field for user-visible situations and
 * the format_id field for fixed-length fields on disk.
 */
/* An unknown hash function. */
#define GIT_HASH_UNKNOWN 0
/* SHA-1 */
#define GIT_HASH_SHA1 1
/* SHA-256  */
#define GIT_HASH_SHA256 2
/* Number of algorithms supported (including unknown). */
#define GIT_HASH_NALGOS (GIT_HASH_SHA256 + 1)

/* A suitably aligned type for stack allocations of hash contexts. */
union git_hash_ctx {
	git_SHA_CTX sha1;
	git_SHA256_CTX sha256;
};
typedef union git_hash_ctx git_hash_ctx;

typedef void (*git_hash_init_fn)(git_hash_ctx *ctx);
typedef void (*git_hash_clone_fn)(git_hash_ctx *dst, const git_hash_ctx *src);
typedef void (*git_hash_update_fn)(git_hash_ctx *ctx, const void *in, size_t len);
typedef void (*git_hash_final_fn)(unsigned char *hash, git_hash_ctx *ctx);

struct git_hash_algo {
	/*
	 * The name of the algorithm, as appears in the config file and in
	 * messages.
	 */
	const char *name;

	/* A four-byte version identifier, used in pack indices. */
	uint32_t format_id;

	/* The length of the hash in binary. */
	size_t rawsz;

	/* The length of the hash in hex characters. */
	size_t hexsz;

	/* The block size of the hash. */
	size_t blksz;

	/* The hash initialization function. */
	git_hash_init_fn init_fn;

	/* The hash context cloning function. */
	git_hash_clone_fn clone_fn;

	/* The hash update function. */
	git_hash_update_fn update_fn;

	/* The hash finalization function. */
	git_hash_final_fn final_fn;

	/* The OID of the empty tree. */
	const struct object_id *empty_tree;

	/* The OID of the empty blob. */
	const struct object_id *empty_blob;
};
extern const struct git_hash_algo hash_algos[GIT_HASH_NALGOS];

/*
 * Return a GIT_HASH_* constant based on the name.  Returns GIT_HASH_UNKNOWN if
 * the name doesn't match a known algorithm.
 */
int hash_algo_by_name(const char *name);
/* Identical, except based on the format ID. */
int hash_algo_by_id(uint32_t format_id);
/* Identical, except based on the length. */
int hash_algo_by_length(int len);
/* Identical, except for a pointer to struct git_hash_algo. */
static inline int hash_algo_by_ptr(const struct git_hash_algo *p)
{
	return p - hash_algos;
}

/* The length in bytes and in hex digits of an object name (SHA-1 value). */
#define GIT_SHA1_RAWSZ 20
#define GIT_SHA1_HEXSZ (2 * GIT_SHA1_RAWSZ)
/* The block size of SHA-1. */
#define GIT_SHA1_BLKSZ 64

/* The length in bytes and in hex digits of an object name (SHA-256 value). */
#define GIT_SHA256_RAWSZ 32
#define GIT_SHA256_HEXSZ (2 * GIT_SHA256_RAWSZ)
/* The block size of SHA-256. */
#define GIT_SHA256_BLKSZ 64

/* The length in byte and in hex digits of the largest possible hash value. */
#define GIT_MAX_RAWSZ GIT_SHA256_RAWSZ
#define GIT_MAX_HEXSZ GIT_SHA256_HEXSZ
/* The largest possible block size for any supported hash. */
#define GIT_MAX_BLKSZ GIT_SHA256_BLKSZ

struct object_id {
	unsigned char hash[GIT_MAX_RAWSZ];
};

#define the_hash_algo the_repository->hash_algo

#endif
