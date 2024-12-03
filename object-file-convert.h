#ifndef OBJECT_CONVERT_H
#define OBJECT_CONVERT_H

struct repository;
struct object_id;
struct git_hash_algo;
struct strbuf;
#include "object.h"

int repo_oid_to_algop(struct repository *repo, const struct object_id *src,
		      const struct git_hash_algo *to, struct object_id *dest);

/*
 * Convert an object file from one hash algorithm to another algorithm.
 * Return -1 on failure, 0 on success.
 */
int convert_object_file(struct strbuf *outbuf,
			const struct git_hash_algo *from,
			const struct git_hash_algo *to,
			const void *buf, size_t len,
			enum object_type type,
			int gentle);

#endif /* OBJECT_CONVERT_H */
