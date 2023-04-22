#ifndef HASH_H
#define HASH_H

#include "hash-ll.h"
#include "repository.h"

#define the_hash_algo the_repository->hash_algo

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

/* Like oidcpy() but zero-pads the unused bytes in dst's hash array. */
static inline void oidcpy_with_padding(struct object_id *dst,
				       const struct object_id *src)
{
	size_t hashsz;

	if (!src->algo)
		hashsz = the_hash_algo->rawsz;
	else
		hashsz = hash_algos[src->algo].rawsz;

	memcpy(dst->hash, src->hash, hashsz);
	memset(dst->hash + hashsz, 0, GIT_MAX_RAWSZ - hashsz);
	dst->algo = src->algo;
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

#endif
