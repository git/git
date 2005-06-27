#ifndef CSUM_FILE_H
#define CSUM_FILE_H

/* A SHA1-protected file */
struct sha1file {
	int fd, error;
	unsigned long offset;
	SHA_CTX ctx;
	unsigned char buffer[8192];
};

extern struct sha1file *sha1create(const char *fmt, ...);
extern int sha1close(struct sha1file *);
extern int sha1write(struct sha1file *, void *, unsigned int);
extern int sha1write_compressed(struct sha1file *, void *, unsigned int);

#endif
