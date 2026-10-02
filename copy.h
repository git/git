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

/*
 * Create "dst" as a copy-on-write (reflink) clone of the regular file
 * "src", so that the two files share their data blocks until one of
 * them is modified. "dst" must not already exist.
 *
 * This only succeeds on filesystems that support block cloning (e.g.
 * btrfs, XFS or bcachefs on Linux, APFS on macOS). When the platform or
 * filesystem does not support reflinks, -1 is returned with errno set to
 * ENOSYS (or the underlying error). Callers are expected to fall back to
 * copy_file() in that case. Returns 0 on success.
 */
int reflink_file(const char *dst, const char *src, int mode);

#endif /* COPY_H */
