#ifndef PACK_H
#define PACK_H

/*
 * The packed object type is stored in 3 bits.
 * The type value 0 is a reserved prefix if ever there is more than 7
 * object types, or any future format extensions.
 */
enum object_type {
	OBJ_EXT = 0,
	OBJ_COMMIT = 1,
	OBJ_TREE = 2,
	OBJ_BLOB = 3,
	OBJ_TAG = 4,
	/* 5/6 for future expansion */
	OBJ_DELTA = 7,
};

/*
 * Packed object header
 */
#define PACK_SIGNATURE 0x5041434b	/* "PACK" */
#define PACK_VERSION 2
struct pack_header {
	unsigned int hdr_signature;
	unsigned int hdr_version;
	unsigned int hdr_entries;
};

extern int verify_pack(struct packed_git *, int);

#endif
