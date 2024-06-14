#ifndef HASH_H
#define HASH_H

#include "hash-ll.h"
#include "repository.h"

#define the_hash_algo the_repository->hash_algo

static inline int is_empty_blob_oid(const struct object_id *oid)
{
	return oideq(oid, the_hash_algo->empty_blob);
}

static inline int is_empty_tree_oid(const struct object_id *oid)
{
	return oideq(oid, the_hash_algo->empty_tree);
}

#endif
