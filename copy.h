#ifndef COPY_H
#define COPY_H

struct repository;

#define COPY_READ_ERROR (-2)
#define COPY_WRITE_ERROR (-3)
int copy_fd(int ifd, int ofd);
int copy_file(struct repository *repo,
	      const char *dst, const char *src, int mode);
int copy_file_with_time(struct repository *repo,
			const char *dst, const char *src, int mode);

#endif /* COPY_H */
