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
 * Packed object index header
 *
 * struct pack_idx_header {
 * 	uint32_t idx_signature;
 *	uint32_t idx_version;
 * };
 *
 * Note: this header isn't active yet.  In future versions of git
 * we may change the index file format.  At that time we would start
 * the first four bytes of the new index format with this signature,
 * as all older git binaries would find this value illegal and abort
 * reading the file.
 *
 * This is the case because the number of objects in a packfile
 * cannot exceed 1,431,660,000 as every object would need at least
 * 3 bytes of data and the overall packfile cannot exceed 4 GiB due
 * to the 32 bit offsets used by the index.  Clearly the signature
 * exceeds this maximum.
 *
 * Very old git binaries will also compare the first 4 bytes to the
 * next 4 bytes in the index and abort with a "non-monotonic index"
 * error if the second 4 byte word is smaller than the first 4
 * byte word.  This would be true in the proposed future index
 * format as idx_signature would be greater than idx_version.
 */
#define PACK_IDX_SIGNATURE 0xff744f63	/* "\377tOc" */

extern int verify_pack(struct packed_git *, int);
#endif
