#ifndef GIT_ZLIB_H
#define GIT_ZLIB_H

typedef struct git_zstream {
	z_stream z;
	unsigned long avail_in;
	unsigned long avail_out;
	unsigned long total_in;
	unsigned long total_out;
	unsigned char *next_in;
	unsigned char *next_out;
} git_zstream;

void git_inflate_init(git_zstream *);
void git_inflate_init_gzip_only(git_zstream *);
void git_inflate_end(git_zstream *);
int git_inflate(git_zstream *, int flush);

void git_deflate_init(git_zstream *, int level);
void git_deflate_init_gzip(git_zstream *, int level);
void git_deflate_init_raw(git_zstream *, int level);
void git_deflate_end(git_zstream *);
int git_deflate_abort(git_zstream *);
int git_deflate_end_gently(git_zstream *);
int git_deflate(git_zstream *, int flush);
unsigned long git_deflate_bound(git_zstream *, unsigned long);

#endif /* GIT_ZLIB_H */
