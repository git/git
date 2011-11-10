#ifndef CSUM_FILE_H
#define CSUM_FILE_H

struct progress;

/* A SHA1-protected file */
struct sha1file {
	int fd;
	unsigned int offset;
	git_SHA_CTX ctx;
	off_t total;
	struct progress *tp;
	const char *name;
	int do_crc;
	uint32_t crc32;
	unsigned char buffer[8192];
};

/* sha1close flags */
#define CSUM_CLOSE	1
#define CSUM_FSYNC	2

extern struct sha1file *sha1fd(int fd, const char *name);
extern struct sha1file *sha1fd_throughput(int fd, const char *name, struct progress *tp);
extern int sha1close(struct sha1file *, unsigned char *, unsigned int);
extern int sha1write(struct sha1file *, void *, unsigned int);
extern void sha1flush(struct sha1file *f);
extern void crc32_begin(struct sha1file *);
extern uint32_t crc32_end(struct sha1file *);

#endif
