#ifndef LOOSE_H
#define LOOSE_H

#include "khash.h"

/* Should we write this object to disk? */
#define OBJ_MAP_WRITE            (1 << 6)

struct repository;
struct odb_source_loose;

struct loose_object_map {
	kh_oid_map_t *to_compat;
	kh_oid_map_t *to_storage;
};

void loose_object_map_init(struct loose_object_map **map);
void loose_object_map_clear(struct loose_object_map **map);
int loose_object_map_load(struct odb_source_loose *loose);
int repo_loose_object_map_oid(struct repository *repo,
			      const struct object_id *src,
			      const struct git_hash_algo *dest_algo,
			      struct object_id *dest);
int repo_read_loose_object_map(struct repository *repo);
int repo_write_loose_object_map(struct repository *repo);

int source_loose_object_map_oid(struct odb_source_loose *loose,
				const struct object_id *src,
				const struct git_hash_algo *to,
				struct object_id *dest);
int source_add_loose_object_map(struct odb_source_loose *loose,
				const struct object_id *oid,
				const struct object_id *compat_oid,
				int flags);

#endif
