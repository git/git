#ifndef PACK_H
#define PACK_H

enum object_type {
	OBJ_NONE,
	OBJ_COMMIT,
	OBJ_TREE,
	OBJ_BLOB,
	OBJ_TAG,
	OBJ_DELTA,
};

/*
 * Packed object header
 */
#define PACK_SIGNATURE 0x5041434b	/* "PACK" */
struct pack_header {
	unsigned int hdr_signature;
	unsigned int hdr_version;
	unsigned int hdr_entries;
};

#endif
