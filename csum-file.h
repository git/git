#ifndef CSUM_FILE_H
#define CSUM_FILE_H

/* A SHA1-protected file */
struct sha1file {
	int fd, error;
	unsigned int offset, namelen;
	SHA_CTX ctx;
	char name[PATH_MAX];
	int do_crc;
	uint32_t crc32;
	unsigned char buffer[8192];
};

extern struct sha1file *sha1fd(int fd, const char *name);
extern int sha1close(struct sha1file *, unsigned char *, int);
extern int sha1write(struct sha1file *, void *, unsigned int);
extern void crc32_begin(struct sha1file *);
extern uint32_t crc32_end(struct sha1file *);

#endif
