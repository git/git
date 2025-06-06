#ifndef MIDX_H
#define MIDX_H

#include "string-list.h"

struct object_id;
struct pack_entry;
struct repository;
struct bitmapped_pack;
struct git_hash_algo;

#define MIDX_SIGNATURE 0x4d494458 /* "MIDX" */
#define MIDX_VERSION 1
#define MIDX_BYTE_FILE_VERSION 4
#define MIDX_BYTE_HASH_VERSION 5
#define MIDX_BYTE_NUM_CHUNKS 6
#define MIDX_BYTE_NUM_PACKS 8
#define MIDX_HEADER_SIZE 12

#define MIDX_CHUNK_ALIGNMENT 4
#define MIDX_CHUNKID_PACKNAMES 0x504e414d /* "PNAM" */
#define MIDX_CHUNKID_BITMAPPEDPACKS 0x42544d50 /* "BTMP" */
#define MIDX_CHUNKID_OIDFANOUT 0x4f494446 /* "OIDF" */
#define MIDX_CHUNKID_OIDLOOKUP 0x4f49444c /* "OIDL" */
#define MIDX_CHUNKID_OBJECTOFFSETS 0x4f4f4646 /* "OOFF" */
#define MIDX_CHUNKID_LARGEOFFSETS 0x4c4f4646 /* "LOFF" */
#define MIDX_CHUNKID_REVINDEX 0x52494458 /* "RIDX" */
#define MIDX_CHUNKID_BASE 0x42415345 /* "BASE" */
#define MIDX_CHUNK_OFFSET_WIDTH (2 * sizeof(uint32_t))
#define MIDX_LARGE_OFFSET_NEEDED 0x80000000

#define GIT_TEST_MULTI_PACK_INDEX "GIT_TEST_MULTI_PACK_INDEX"
#define GIT_TEST_MULTI_PACK_INDEX_WRITE_INCREMENTAL \
	"GIT_TEST_MULTI_PACK_INDEX_WRITE_INCREMENTAL"

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
	int preferred_pack_idx;

	int local;
	int has_chain;

	const unsigned char *chunk_pack_names;
	size_t chunk_pack_names_len;
	const uint32_t *chunk_bitmapped_packs;
	size_t chunk_bitmapped_packs_len;
	const uint32_t *chunk_oid_fanout;
	const unsigned char *chunk_oid_lookup;
	const unsigned char *chunk_object_offsets;
	const unsigned char *chunk_large_offsets;
	size_t chunk_large_offsets_len;
	const unsigned char *chunk_revindex;
	size_t chunk_revindex_len;

	struct multi_pack_index *base_midx;
	uint32_t num_objects_in_base;
	uint32_t num_packs_in_base;

	const char **pack_names;
	struct packed_git **packs;

	struct repository *repo;

	char object_dir[FLEX_ARRAY];
};

#define MIDX_PROGRESS     (1 << 0)
#define MIDX_WRITE_REV_INDEX (1 << 1)
#define MIDX_WRITE_BITMAP (1 << 2)
#define MIDX_WRITE_BITMAP_HASH_CACHE (1 << 3)
#define MIDX_WRITE_BITMAP_LOOKUP_TABLE (1 << 4)
#define MIDX_WRITE_INCREMENTAL (1 << 5)

#define MIDX_EXT_REV "rev"
#define MIDX_EXT_BITMAP "bitmap"
#define MIDX_EXT_MIDX "midx"

const unsigned char *get_midx_checksum(struct multi_pack_index *m);
void get_midx_filename(const struct git_hash_algo *hash_algo,
		       struct strbuf *out, const char *object_dir);
void get_midx_filename_ext(const struct git_hash_algo *hash_algo,
			   struct strbuf *out, const char *object_dir,
			   const unsigned char *hash, const char *ext);
void get_midx_chain_dirname(struct strbuf *buf, const char *object_dir);
void get_midx_chain_filename(struct strbuf *buf, const char *object_dir);
void get_split_midx_filename_ext(const struct git_hash_algo *hash_algo,
				 struct strbuf *buf, const char *object_dir,
				 const unsigned char *hash, const char *ext);

struct multi_pack_index *load_multi_pack_index(struct repository *r,
					       const char *object_dir,
					       int local);
struct packed_git *prepare_midx_pack(struct repository *r,
				     struct multi_pack_index *m,
				     uint32_t pack_int_id);
struct packed_git *nth_midxed_pack(struct multi_pack_index *m,
				   uint32_t pack_int_id);
const char *nth_midxed_pack_name(struct multi_pack_index *m,
				 uint32_t pack_int_id);
int nth_bitmapped_pack(struct repository *r, struct multi_pack_index *m,
		       struct bitmapped_pack *bp, uint32_t pack_int_id);
int bsearch_one_midx(const struct object_id *oid, struct multi_pack_index *m,
		     uint32_t *result);
int bsearch_midx(const struct object_id *oid, struct multi_pack_index *m,
		 uint32_t *result);
int midx_has_oid(struct multi_pack_index *m, const struct object_id *oid);
off_t nth_midxed_offset(struct multi_pack_index *m, uint32_t pos);
uint32_t nth_midxed_pack_int_id(struct multi_pack_index *m, uint32_t pos);
struct object_id *nth_midxed_object_oid(struct object_id *oid,
					struct multi_pack_index *m,
					uint32_t n);
int fill_midx_entry(struct repository *r, const struct object_id *oid, struct pack_entry *e, struct multi_pack_index *m);
int midx_contains_pack(struct multi_pack_index *m,
		       const char *idx_or_pack_name);
int midx_preferred_pack(struct multi_pack_index *m, uint32_t *pack_int_id);
int prepare_multi_pack_index_one(struct repository *r, const char *object_dir, int local);

/*
 * Variant of write_midx_file which writes a MIDX containing only the packs
 * specified in packs_to_include.
 */
int write_midx_file(struct repository *r, const char *object_dir,
		    const char *preferred_pack_name, const char *refs_snapshot,
		    unsigned flags);
int write_midx_file_only(struct repository *r, const char *object_dir,
			 struct string_list *packs_to_include,
			 const char *preferred_pack_name,
			 const char *refs_snapshot, unsigned flags);
void clear_midx_file(struct repository *r);
int verify_midx_file(struct repository *r, const char *object_dir, unsigned flags);
int expire_midx_packs(struct repository *r, const char *object_dir, unsigned flags);
int midx_repack(struct repository *r, const char *object_dir, size_t batch_size, unsigned flags);

void close_midx(struct multi_pack_index *m);

#endif
