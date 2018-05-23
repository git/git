#ifndef PACK_OBJECTS_H
#define PACK_OBJECTS_H

#define DEFAULT_DELTA_CACHE_SIZE (256 * 1024 * 1024)

struct object_entry {
	struct pack_idx_entry idx;
	unsigned long size;	/* uncompressed size */
	struct packed_git *in_pack;	/* already in pack */
	off_t in_pack_offset;
	struct object_entry *delta;	/* delta base object */
	struct object_entry *delta_child; /* deltified objects who bases me */
	struct object_entry *delta_sibling; /* other deltified objects who
					     * uses the same base as me
					     */
	void *delta_data;	/* cached delta (uncompressed) */
	unsigned long delta_size;	/* delta data size (uncompressed) */
	unsigned long z_delta_size;	/* delta data size (compressed) */
	enum object_type type;
	enum object_type in_pack_type;	/* could be delta */
	uint32_t hash;			/* name hint hash */
	unsigned int in_pack_pos;
	unsigned char in_pack_header_size;
	unsigned preferred_base:1; /*
				    * we do not pack this, but is available
				    * to be used as the base object to delta
				    * objects against.
				    */
	unsigned no_try_delta:1;
	unsigned tagged:1; /* near the very tip of refs */
	unsigned filled:1; /* assigned write-order */

	/*
	 * State flags for depth-first search used for analyzing delta cycles.
	 *
	 * The depth is measured in delta-links to the base (so if A is a delta
	 * against B, then A has a depth of 1, and B a depth of 0).
	 */
	enum {
		DFS_NONE = 0,
		DFS_ACTIVE,
		DFS_DONE
	} dfs_state;
	int depth;
};

struct packing_data {
	struct object_entry *objects;
	uint32_t nr_objects, nr_alloc;

	int32_t *index;
	uint32_t index_size;
};

struct object_entry *packlist_alloc(struct packing_data *pdata,
				    const unsigned char *sha1,
				    uint32_t index_pos);

struct object_entry *packlist_find(struct packing_data *pdata,
				   const unsigned char *sha1,
				   uint32_t *index_pos);

static inline uint32_t pack_name_hash(const char *name)
{
	uint32_t c, hash = 0;

	if (!name)
		return 0;

	/*
	 * This effectively just creates a sortable number from the
	 * last sixteen non-whitespace characters. Last characters
	 * count "most", so things that end in ".c" sort together.
	 */
	while ((c = *name++) != 0) {
		if (isspace(c))
			continue;
		hash = (hash >> 2) + (c << 24);
	}
	return hash;
}

#endif
