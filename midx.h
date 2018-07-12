#ifndef __MIDX_H__
#define __MIDX_H__

struct multi_pack_index {
	int fd;

	const unsigned char *data;
	size_t data_len;

	uint32_t signature;
	unsigned char version;
	unsigned char hash_len;
	unsigned char num_chunks;
	uint32_t num_packs;
	uint32_t num_objects;

	const unsigned char *chunk_pack_names;
	const uint32_t *chunk_oid_fanout;
	const unsigned char *chunk_oid_lookup;
	const unsigned char *chunk_object_offsets;
	const unsigned char *chunk_large_offsets;

	const char **pack_names;
	char object_dir[FLEX_ARRAY];
};

struct multi_pack_index *load_multi_pack_index(const char *object_dir);

int write_midx_file(const char *object_dir);

#endif
