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

#ifdef SHA1_MAX_BLOCK_SIZE
#include "compat/sha1-chunked.h"
#undef git_SHA1_Update
#define git_SHA1_Update		git_SHA1_Update_Chunked
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
/* Number of algorithms supported (including unknown). */
#define GIT_HASH_NALGOS (GIT_HASH_SHA1 + 1)

/* A suitably aligned type for stack allocations of hash contexts. */
union git_hash_ctx {
	git_SHA_CTX sha1;
};
typedef union git_hash_ctx git_hash_ctx;

typedef void (*git_hash_init_fn)(git_hash_ctx *ctx);
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

	/* The hash initialization function. */
	git_hash_init_fn init_fn;

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

#endif
