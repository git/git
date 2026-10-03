#ifndef PACK_COMPAT_NAMES_H
#define PACK_COMPAT_NAMES_H

#include "hash.h"
#include "stdint.h"

struct object_id;
struct packed_git;
struct repository;

/*
 * The compatibility object names of the objects in a packfile.
 *
 * Objects are named by a hash of their content, so an object stored in a
 * repository's object format is named differently there than it is in the
 * repository's compatibility object format.  These names are recorded next
 * to the pack index, per packfile, in the same order as the names in the
 * index, so that both directions of the mapping can be looked up.
 *
 * See linkgit:gitformat-pack[5] for the file format.
 */
#define COMPAT_SIGNATURE 0x434d5054 /* "CMPT" */
#define COMPAT_VERSION 1

struct compat_header {
	uint32_t signature;
	uint32_t version;
	uint32_t hash_version;
	uint32_t nr_objects;
	uint32_t rawsz;
};

#define COMPAT_HEADER_SIZE (sizeof(struct compat_header))

/*
 * Returns the name that the object at position `nr` of the pack index has in
 * the compatibility object format, or -1 if it has none recorded.
 */
int packed_object_compat_oid(struct packed_git *p, uint32_t nr,
			     struct object_id *compat_oid);

void close_pack_compat_names(struct packed_git *p);

/*
 * Returns the index position of the object named `compat_oid` in the
 * compatibility object format, or -1 if this packfile has no such object.
 */
int packed_object_by_compat_oid(struct packed_git *p,
				const struct object_id *compat_oid,
				uint32_t *nr);

#endif /* PACK_COMPAT_NAMES_H */