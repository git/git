#ifndef PACK_OBJECTS_H
#define PACK_OBJECTS_H

#include "object-store.h"
#include "thread-utils.h"
#include "pack.h"

struct repository;

#define DEFAULT_DELTA_CACHE_SIZE (256 * 1024 * 1024)

#define OE_DFS_STATE_BITS	2
#define OE_DEPTH_BITS		12
#define OE_IN_PACK_BITS		10
#define OE_Z_DELTA_BITS		20
/*
 * Note that oe_set_size() becomes expensive when the given size is
 * above this limit. Don't lower it too much.
 */
#define OE_SIZE_BITS		31
#define OE_DELTA_SIZE_BITS	23

/*
 * State flags for depth-first search used for analyzing delta cycles.
 *
 * The depth is measured in delta-links to the base (so if A is a delta
 * against B, then A has a depth of 1, and B a depth of 0).
 */
enum dfs_state {
	DFS_NONE = 0,
	DFS_ACTIVE,
	DFS_DONE,
	DFS_NUM_STATES
};

/*
 * The size of struct nearly determines pack-objects's memory
 * consumption. This struct is packed tight for that reason. When you
 * add or reorder something in this struct, think a bit about this.
 *
 * basic object info
 * -----------------
 * idx.oid is filled up before delta searching starts. idx.crc32 is
 * only valid after the object is written out and will be used for
 * generating the index. idx.offset will be both gradually set and
 * used in writing phase (base objects get offset first, then deltas
 * refer to them)
 *
 * "size" is the uncompressed object size. Compressed size of the raw
 * data for an object in a pack is not stored anywhere but is computed
 * and made available when reverse .idx is made. Note that when a
 * delta is reused, "size" is the uncompressed _delta_ size, not the
 * canonical one after the delta has been applied.
 *
 * "hash" contains a path name hash which is used for sorting the
 * delta list and also during delta searching. Once prepare_pack()
 * returns it's no longer needed.
 *
 * source pack info
 * ----------------
 * The (in_pack, in_pack_offset) tuple contains the location of the
 * object in the source pack. in_pack_header_size allows quickly
 * skipping the header and going straight to the zlib stream.
 *
 * "type" and "in_pack_type" both describe object type. in_pack_type
 * may contain a delta type, while type is always the canonical type.
 *
 * deltas
 * ------
 * Delta links (delta, delta_child and delta_sibling) are created to
 * reflect that delta graph from the source pack then updated or added
 * during delta searching phase when we find better deltas.
 *
 * delta_child and delta_sibling are last needed in
 * compute_write_order(). "delta" and "delta_size" must remain valid
 * at object writing phase in case the delta is not cached.
 *
 * If a delta is cached in memory and is compressed, delta_data points
 * to the data and z_delta_size contains the compressed size. If it's
 * uncompressed [1], z_delta_size must be zero. delta_size is always
 * the uncompressed size and must be valid even if the delta is not
 * cached.
 *
 * [1] during try_delta phase we don't bother with compressing because
 * the delta could be quickly replaced with a better one.
 */
struct object_entry {
	struct pack_idx_entry idx;
	void *delta_data;	/* cached delta (uncompressed) */
	off_t in_pack_offset;
	uint32_t hash;			/* name hint hash */
	unsigned size_:OE_SIZE_BITS;
	unsigned size_valid:1;
	uint32_t delta_idx;	/* delta base object */
	uint32_t delta_child_idx; /* deltified objects who bases me */
	uint32_t delta_sibling_idx; /* other deltified objects who
				     * uses the same base as me
				     */
	unsigned delta_size_:OE_DELTA_SIZE_BITS; /* delta data size (uncompressed) */
	unsigned delta_size_valid:1;
	unsigned char in_pack_header_size;
	unsigned in_pack_idx:OE_IN_PACK_BITS;	/* already in pack */
	unsigned z_delta_size:OE_Z_DELTA_BITS;
	unsigned type_valid:1;
	unsigned no_try_delta:1;
	unsigned type_:TYPE_BITS;
	unsigned in_pack_type:TYPE_BITS; /* could be delta */

	unsigned preferred_base:1; /*
				    * we do not pack this, but is available
				    * to be used as the base object to delta
				    * objects against.
				    */
	unsigned tagged:1; /* near the very tip of refs */
	unsigned filled:1; /* assigned write-order */
	unsigned dfs_state:OE_DFS_STATE_BITS;
	unsigned depth:OE_DEPTH_BITS;
	unsigned ext_base:1; /* delta_idx points outside packlist */

	/*
	 * pahole results on 64-bit linux (gcc and clang)
	 *
	 *   size: 80, bit_padding: 9 bits
	 *
	 * and on 32-bit (gcc)
	 *
	 *   size: 76, bit_padding: 9 bits
	 */
};

struct packing_data {
	struct repository *repo;
	struct object_entry *objects;
	uint32_t nr_objects, nr_alloc;

	int32_t *index;
	uint32_t index_size;

	unsigned int *in_pack_pos;
	unsigned long *delta_size;

	/*
	 * Only one of these can be non-NULL and they have different
	 * sizes. if in_pack_by_idx is allocated, oe_in_pack() returns
	 * the pack of an object using in_pack_idx field. If not,
	 * in_pack[] array is used the same way as in_pack_pos[]
	 */
	struct packed_git **in_pack_by_idx;
	struct packed_git **in_pack;

	/*
	 * During packing with multiple threads, protect the in-core
	 * object database from concurrent accesses.
	 */
	pthread_mutex_t odb_lock;

	/*
	 * This list contains entries for bases which we know the other side
	 * has (e.g., via reachability bitmaps), but which aren't in our
	 * "objects" list.
	 */
	struct object_entry *ext_bases;
	uint32_t nr_ext, alloc_ext;

	uintmax_t oe_size_limit;
	uintmax_t oe_delta_size_limit;

	/* delta islands */
	unsigned int *tree_depth;
	unsigned char *layer;
};

void prepare_packing_data(struct repository *r, struct packing_data *pdata);

/* Protect access to object database */
static inline void packing_data_lock(struct packing_data *pdata)
{
	pthread_mutex_lock(&pdata->odb_lock);
}
static inline void packing_data_unlock(struct packing_data *pdata)
{
	pthread_mutex_unlock(&pdata->odb_lock);
}

struct object_entry *packlist_alloc(struct packing_data *pdata,
				    const struct object_id *oid);

struct object_entry *packlist_find(struct packing_data *pdata,
				   const struct object_id *oid);

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

static inline enum object_type oe_type(const struct object_entry *e)
{
	return e->type_valid ? e->type_ : OBJ_BAD;
}

static inline void oe_set_type(struct object_entry *e,
			       enum object_type type)
{
	if (type >= OBJ_ANY)
		BUG("OBJ_ANY cannot be set in pack-objects code");

	e->type_valid = type >= OBJ_NONE;
	e->type_ = (unsigned)type;
}

static inline unsigned int oe_in_pack_pos(const struct packing_data *pack,
					  const struct object_entry *e)
{
	return pack->in_pack_pos[e - pack->objects];
}

static inline void oe_set_in_pack_pos(const struct packing_data *pack,
				      const struct object_entry *e,
				      unsigned int pos)
{
	pack->in_pack_pos[e - pack->objects] = pos;
}

static inline struct packed_git *oe_in_pack(const struct packing_data *pack,
					    const struct object_entry *e)
{
	if (pack->in_pack_by_idx)
		return pack->in_pack_by_idx[e->in_pack_idx];
	else
		return pack->in_pack[e - pack->objects];
}

void oe_map_new_pack(struct packing_data *pack);

static inline void oe_set_in_pack(struct packing_data *pack,
				  struct object_entry *e,
				  struct packed_git *p)
{
	if (pack->in_pack_by_idx) {
		if (p->index) {
			e->in_pack_idx = p->index;
			return;
		}
		/*
		 * We're accessing packs by index, but this pack doesn't have
		 * an index (e.g., because it was added since we created the
		 * in_pack_by_idx array). Bail to oe_map_new_pack(), which
		 * will convert us to using the full in_pack array, and then
		 * fall through to our in_pack handling.
		 */
		oe_map_new_pack(pack);
	}
	pack->in_pack[e - pack->objects] = p;
}

void oe_set_delta_ext(struct packing_data *pack,
		      struct object_entry *e,
		      const struct object_id *oid);

static inline unsigned int oe_tree_depth(struct packing_data *pack,
					 struct object_entry *e)
{
	if (!pack->tree_depth)
		return 0;
	return pack->tree_depth[e - pack->objects];
}

static inline void oe_set_layer(struct packing_data *pack,
				struct object_entry *e,
				unsigned char layer)
{
	if (!pack->layer)
		CALLOC_ARRAY(pack->layer, pack->nr_alloc);
	pack->layer[e - pack->objects] = layer;
}

#endif
