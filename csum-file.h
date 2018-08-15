#ifndef CSUM_FILE_H
#define CSUM_FILE_H

#include "hash.h"

struct progress;

/* A SHA1-protected file */
struct hashfile {
	int fd;
	int check_fd;
	unsigned int offset;
	git_hash_ctx ctx;
	off_t total;
	struct progress *tp;
	const char *name;
	int do_crc;
	uint32_t crc32;
	unsigned char buffer[8192];
};

/* Checkpoint */
struct hashfile_checkpoint {
	off_t offset;
	git_hash_ctx ctx;
};

extern void hashfile_checkpoint(struct hashfile *, struct hashfile_checkpoint *);
extern int hashfile_truncate(struct hashfile *, struct hashfile_checkpoint *);

/* finalize_hashfile flags */
#define CSUM_CLOSE		1
#define CSUM_FSYNC		2
#define CSUM_HASH_IN_STREAM	4

extern struct hashfile *hashfd(int fd, const char *name);
extern struct hashfile *hashfd_check(const char *name);
extern struct hashfile *hashfd_throughput(int fd, const char *name, struct progress *tp);
extern int finalize_hashfile(struct hashfile *, unsigned char *, unsigned int);
extern void hashwrite(struct hashfile *, const void *, unsigned int);
extern void hashflush(struct hashfile *f);
extern void crc32_begin(struct hashfile *);
extern uint32_t crc32_end(struct hashfile *);

static inline void hashwrite_u8(struct hashfile *f, uint8_t data)
{
	hashwrite(f, &data, sizeof(data));
}

static inline void hashwrite_be32(struct hashfile *f, uint32_t data)
{
	data = htonl(data);
	hashwrite(f, &data, sizeof(data));
}

#endif
