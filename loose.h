#ifndef LOOSE_H
#define LOOSE_H

#include "khash.h"

struct loose_object_map {
	kh_oid_map_t *to_compat;
	kh_oid_map_t *to_storage;
};

void loose_object_map_init(struct loose_object_map **map);
void loose_object_map_clear(struct loose_object_map **map);
int repo_loose_object_map_oid(struct repository *repo,
			      const struct object_id *src,
			      const struct git_hash_algo *dest_algo,
			      struct object_id *dest);
int repo_add_loose_object_map(struct repository *repo, const struct object_id *oid,
			      const struct object_id *compat_oid);
int repo_read_loose_object_map(struct repository *repo);
int repo_write_loose_object_map(struct repository *repo);

#endif
