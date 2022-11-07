#ifndef CSUM_FILE_H
#define CSUM_FILE_H

#include "cache.h"
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
	size_t buffer_len;
	unsigned char *buffer;
	unsigned char *check_buffer;

	/**
	 * If set to 1, skip_hash indicates that we should
	 * not actually compute the hash for this hashfile and
	 * instead only use it as a buffered write.
	 */
	unsigned int skip_hash;
};

/* Checkpoint */
struct hashfile_checkpoint {
	off_t offset;
	git_hash_ctx ctx;
};

void hashfile_checkpoint(struct hashfile *, struct hashfile_checkpoint *);
int hashfile_truncate(struct hashfile *, struct hashfile_checkpoint *);

/* finalize_hashfile flags */
#define CSUM_CLOSE		1
#define CSUM_FSYNC		2
#define CSUM_HASH_IN_STREAM	4

struct hashfile *hashfd(int fd, const char *name);
struct hashfile *hashfd_check(const char *name);
struct hashfile *hashfd_throughput(int fd, const char *name, struct progress *tp);
int finalize_hashfile(struct hashfile *, unsigned char *, enum fsync_component, unsigned int);
void hashwrite(struct hashfile *, const void *, unsigned int);
void hashflush(struct hashfile *f);
void crc32_begin(struct hashfile *);
uint32_t crc32_end(struct hashfile *);

/* Verify checksum validity while reading. Returns non-zero on success. */
int hashfile_checksum_valid(const unsigned char *data, size_t len);

/*
 * Returns the total number of bytes fed to the hashfile so far (including ones
 * that have not been written out to the descriptor yet).
 */
static inline off_t hashfile_total(struct hashfile *f)
{
	return f->total + f->offset;
}

static inline void hashwrite_u8(struct hashfile *f, uint8_t data)
{
	hashwrite(f, &data, sizeof(data));
}

static inline void hashwrite_be32(struct hashfile *f, uint32_t data)
{
	data = htonl(data);
	hashwrite(f, &data, sizeof(data));
}

static inline size_t hashwrite_be64(struct hashfile *f, uint64_t data)
{
	data = htonll(data);
	hashwrite(f, &data, sizeof(data));
	return sizeof(data);
}

#endif
