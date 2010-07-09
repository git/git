#ifndef PACK_H
#define PACK_H

#include "object.h"

/*
 * Packed object header
 */
#define PACK_SIGNATURE 0x5041434b	/* "PACK" */
#define PACK_VERSION 2
#define pack_version_ok(v) ((v) == htonl(2) || (v) == htonl(3))
struct pack_header {
	uint32_t hdr_signature;
	uint32_t hdr_version;
	uint32_t hdr_entries;
};

/*
 * The first four bytes of index formats later than version 1 should
 * start with this signature, as all older git binaries would find this
 * value illegal and abort reading the file.
 *
 * This is the case because the number of objects in a packfile
 * cannot exceed 1,431,660,000 as every object would need at least
 * 3 bytes of data and the overall packfile cannot exceed 4 GiB with
 * version 1 of the index file due to the offsets limited to 32 bits.
 * Clearly the signature exceeds this maximum.
 *
 * Very old git binaries will also compare the first 4 bytes to the
 * next 4 bytes in the index and abort with a "non-monotonic index"
 * error if the second 4 byte word is smaller than the first 4
 * byte word.  This would be true in the proposed future index
 * format as idx_signature would be greater than idx_version.
 */
#define PACK_IDX_SIGNATURE 0xff744f63	/* "\377tOc" */

/* These may be overridden by command-line parameters */
extern uint32_t pack_idx_default_version;
extern uint32_t pack_idx_off32_limit;

/*
 * Packed object index header
 */
struct pack_idx_header {
	uint32_t idx_signature;
	uint32_t idx_version;
};

/*
 * Common part of object structure used for write_idx_file
 */
struct pack_idx_entry {
	unsigned char sha1[20];
	uint32_t crc32;
	off_t offset;
};

extern const char *write_idx_file(const char *index_name, struct pack_idx_entry **objects, int nr_objects, unsigned char *sha1);
extern int check_pack_crc(struct packed_git *p, struct pack_window **w_curs, off_t offset, off_t len, unsigned int nr);
extern int verify_pack_index(struct packed_git *);
extern int verify_pack(struct packed_git *);
extern void fixup_pack_header_footer(int, unsigned char *, const char *, uint32_t, unsigned char *, off_t);
extern char *index_pack_lockfile(int fd);
extern int encode_in_pack_object_header(enum object_type, uintmax_t, unsigned char *);

#define PH_ERROR_EOF		(-1)
#define PH_ERROR_PACK_SIGNATURE	(-2)
#define PH_ERROR_PROTOCOL	(-3)
extern int read_pack_header(int fd, struct pack_header *);
#endif
