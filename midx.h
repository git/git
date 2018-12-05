#ifndef MIDX_H
#define MIDX_H

#include "repository.h"

struct object_id;
struct pack_entry;
struct repository;

#define GIT_TEST_MULTI_PACK_INDEX "GIT_TEST_MULTI_PACK_INDEX"

struct multi_pack_index {
	struct multi_pack_index *next;

	int fd;

	const unsigned char *data;
	size_t data_len;

	uint32_t signature;
	unsigned char version;
	unsigned char hash_len;
	unsigned char num_chunks;
	uint32_t num_packs;
	uint32_t num_objects;

	int local;

	const unsigned char *chunk_pack_names;
	const uint32_t *chunk_oid_fanout;
	const unsigned char *chunk_oid_lookup;
	const unsigned char *chunk_object_offsets;
	const unsigned char *chunk_large_offsets;

	const char **pack_names;
	struct packed_git **packs;
	char object_dir[FLEX_ARRAY];
};

struct multi_pack_index *load_multi_pack_index(const char *object_dir, int local);
int prepare_midx_pack(struct repository *r, struct multi_pack_index *m, uint32_t pack_int_id);
int bsearch_midx(const struct object_id *oid, struct multi_pack_index *m, uint32_t *result);
struct object_id *nth_midxed_object_oid(struct object_id *oid,
					struct multi_pack_index *m,
					uint32_t n);
int fill_midx_entry(struct repository *r, const struct object_id *oid, struct pack_entry *e, struct multi_pack_index *m);
int midx_contains_pack(struct multi_pack_index *m, const char *idx_or_pack_name);
int prepare_multi_pack_index_one(struct repository *r, const char *object_dir, int local);

int write_midx_file(const char *object_dir);
void clear_midx_file(struct repository *r);
int verify_midx_file(struct repository *r, const char *object_dir);
int expire_midx_packs(struct repository *r, const char *object_dir);
int midx_repack(struct repository *r, const char *object_dir, size_t batch_size);

void close_midx(struct multi_pack_index *m);

#endif
