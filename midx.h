#ifndef MIDX_H
#define MIDX_H

#include "repository.h"
#include "string-list.h"

struct object_id;
struct pack_entry;
struct repository;

#define GIT_TEST_MULTI_PACK_INDEX "GIT_TEST_MULTI_PACK_INDEX"
#define GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP \
	"GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP"

struct multi_pack_index {
	struct multi_pack_index *next;

	const unsigned char *data;
	size_t data_len;

	const uint32_t *revindex_data;
	const uint32_t *revindex_map;
	size_t revindex_len;

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
	const unsigned char *chunk_revindex;

	const char **pack_names;
	struct packed_git **packs;
	char object_dir[FLEX_ARRAY];
};

#define MIDX_PROGRESS     (1 << 0)
#define MIDX_WRITE_REV_INDEX (1 << 1)
#define MIDX_WRITE_BITMAP (1 << 2)
#define MIDX_WRITE_BITMAP_HASH_CACHE (1 << 3)
#define MIDX_WRITE_BITMAP_LOOKUP_TABLE (1 << 4)

const unsigned char *get_midx_checksum(struct multi_pack_index *m);
void get_midx_filename(struct strbuf *out, const char *object_dir);
void get_midx_rev_filename(struct strbuf *out, struct multi_pack_index *m);

struct multi_pack_index *load_multi_pack_index(const char *object_dir, int local);
int prepare_midx_pack(struct repository *r, struct multi_pack_index *m, uint32_t pack_int_id);
int bsearch_midx(const struct object_id *oid, struct multi_pack_index *m, uint32_t *result);
off_t nth_midxed_offset(struct multi_pack_index *m, uint32_t pos);
uint32_t nth_midxed_pack_int_id(struct multi_pack_index *m, uint32_t pos);
struct object_id *nth_midxed_object_oid(struct object_id *oid,
					struct multi_pack_index *m,
					uint32_t n);
int fill_midx_entry(struct repository *r, const struct object_id *oid, struct pack_entry *e, struct multi_pack_index *m);
int midx_contains_pack(struct multi_pack_index *m, const char *idx_or_pack_name);
int prepare_multi_pack_index_one(struct repository *r, const char *object_dir, int local);

/*
 * Variant of write_midx_file which writes a MIDX containing only the packs
 * specified in packs_to_include.
 */
int write_midx_file(const char *object_dir,
		    const char *preferred_pack_name,
		    const char *refs_snapshot,
		    unsigned flags);
int write_midx_file_only(const char *object_dir,
			 struct string_list *packs_to_include,
			 const char *preferred_pack_name,
			 const char *refs_snapshot,
			 unsigned flags);
void clear_midx_file(struct repository *r);
int verify_midx_file(struct repository *r, const char *object_dir, unsigned flags);
int expire_midx_packs(struct repository *r, const char *object_dir, unsigned flags);
int midx_repack(struct repository *r, const char *object_dir, size_t batch_size, unsigned flags);

void close_midx(struct multi_pack_index *m);

#endif
