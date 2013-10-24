#ifndef PACK_OBJECTS_H
#define PACK_OBJECTS_H

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
	unsigned char in_pack_header_size;
	unsigned preferred_base:1; /*
				    * we do not pack this, but is available
				    * to be used as the base object to delta
				    * objects against.
				    */
	unsigned no_try_delta:1;
	unsigned tagged:1; /* near the very tip of refs */
	unsigned filled:1; /* assigned write-order */
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

#endif
