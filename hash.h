#ifndef HASH_H
#define HASH_H

#include "git-compat-util.h"
#include "repository.h"

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
	int algo;
};

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
typedef void (*git_hash_final_oid_fn)(struct object_id *oid, git_hash_ctx *ctx);

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

	/* The hash finalization function for object IDs. */
	git_hash_final_oid_fn final_oid_fn;

	/* The OID of the empty tree. */
	const struct object_id *empty_tree;

	/* The OID of the empty blob. */
	const struct object_id *empty_blob;

	/* The all-zeros OID. */
	const struct object_id *null_oid;
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

#define the_hash_algo the_repository->hash_algo

const struct object_id *null_oid(void);

static inline int hashcmp_algop(const unsigned char *sha1, const unsigned char *sha2, const struct git_hash_algo *algop)
{
	/*
	 * Teach the compiler that there are only two possibilities of hash size
	 * here, so that it can optimize for this case as much as possible.
	 */
	if (algop->rawsz == GIT_MAX_RAWSZ)
		return memcmp(sha1, sha2, GIT_MAX_RAWSZ);
	return memcmp(sha1, sha2, GIT_SHA1_RAWSZ);
}

static inline int hashcmp(const unsigned char *sha1, const unsigned char *sha2)
{
	return hashcmp_algop(sha1, sha2, the_hash_algo);
}

static inline int oidcmp(const struct object_id *oid1, const struct object_id *oid2)
{
	const struct git_hash_algo *algop;
	if (!oid1->algo)
		algop = the_hash_algo;
	else
		algop = &hash_algos[oid1->algo];
	return hashcmp_algop(oid1->hash, oid2->hash, algop);
}

static inline int hasheq_algop(const unsigned char *sha1, const unsigned char *sha2, const struct git_hash_algo *algop)
{
	/*
	 * We write this here instead of deferring to hashcmp so that the
	 * compiler can properly inline it and avoid calling memcmp.
	 */
	if (algop->rawsz == GIT_MAX_RAWSZ)
		return !memcmp(sha1, sha2, GIT_MAX_RAWSZ);
	return !memcmp(sha1, sha2, GIT_SHA1_RAWSZ);
}

static inline int hasheq(const unsigned char *sha1, const unsigned char *sha2)
{
	return hasheq_algop(sha1, sha2, the_hash_algo);
}

static inline int oideq(const struct object_id *oid1, const struct object_id *oid2)
{
	const struct git_hash_algo *algop;
	if (!oid1->algo)
		algop = the_hash_algo;
	else
		algop = &hash_algos[oid1->algo];
	return hasheq_algop(oid1->hash, oid2->hash, algop);
}

static inline int is_null_oid(const struct object_id *oid)
{
	return oideq(oid, null_oid());
}

static inline void hashcpy(unsigned char *sha_dst, const unsigned char *sha_src)
{
	memcpy(sha_dst, sha_src, the_hash_algo->rawsz);
}

static inline void oidcpy(struct object_id *dst, const struct object_id *src)
{
	memcpy(dst->hash, src->hash, GIT_MAX_RAWSZ);
	dst->algo = src->algo;
}

static inline struct object_id *oiddup(const struct object_id *src)
{
	struct object_id *dst = xmalloc(sizeof(struct object_id));
	oidcpy(dst, src);
	return dst;
}

static inline void hashclr(unsigned char *hash)
{
	memset(hash, 0, the_hash_algo->rawsz);
}

static inline void oidclr(struct object_id *oid)
{
	memset(oid->hash, 0, GIT_MAX_RAWSZ);
	oid->algo = hash_algo_by_ptr(the_hash_algo);
}

static inline void oidread(struct object_id *oid, const unsigned char *hash)
{
	memcpy(oid->hash, hash, the_hash_algo->rawsz);
	oid->algo = hash_algo_by_ptr(the_hash_algo);
}

static inline int is_empty_blob_sha1(const unsigned char *sha1)
{
	return hasheq(sha1, the_hash_algo->empty_blob->hash);
}

static inline int is_empty_blob_oid(const struct object_id *oid)
{
	return oideq(oid, the_hash_algo->empty_blob);
}

static inline int is_empty_tree_sha1(const unsigned char *sha1)
{
	return hasheq(sha1, the_hash_algo->empty_tree->hash);
}

static inline int is_empty_tree_oid(const struct object_id *oid)
{
	return oideq(oid, the_hash_algo->empty_tree);
}

static inline void oid_set_algo(struct object_id *oid, const struct git_hash_algo *algop)
{
	oid->algo = hash_algo_by_ptr(algop);
}

const char *empty_tree_oid_hex(void);
const char *empty_blob_oid_hex(void);

#endif
