#include "git-compat-util.h"
#include "copy.h"
#include "path.h"
#include "gettext.h"
#include "strbuf.h"
#include "abspath.h"

#if defined(__linux__)
#include <sys/ioctl.h>
/*
 * FICLONE lives in <linux/fs.h>, but including that header tends to clash
 * with the libc headers git already pulls in, so define it ourselves if it
 * is missing. The value is part of the stable kernel uapi.
 */
#ifndef FICLONE
#define FICLONE _IOW(0x94, 9, int)
#endif
#elif defined(__APPLE__)
#include <sys/attr.h>
#include <sys/clonefile.h>
#endif

int copy_fd(int ifd, int ofd)
{
	while (1) {
		char buffer[8192];
		ssize_t len = xread(ifd, buffer, sizeof(buffer));
		if (!len)
			break;
		if (len < 0)
			return COPY_READ_ERROR;
		if (write_in_full(ofd, buffer, len) < 0)
			return COPY_WRITE_ERROR;
	}
	return 0;
}

static int copy_times(const char *dst, const char *src)
{
	struct stat st;
	struct utimbuf times;
	if (stat(src, &st) < 0)
		return -1;
	times.actime = st.st_atime;
	times.modtime = st.st_mtime;
	if (utime(dst, &times) < 0)
		return -1;
	return 0;
}

int copy_file(struct repository *repo,
	      const char *dst, const char *src, int mode)
{
	int fdi, fdo, status;

	mode = (mode & 0111) ? 0777 : 0666;
	if ((fdi = open(src, O_RDONLY)) < 0)
		return fdi;
	if ((fdo = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode)) < 0) {
		close(fdi);
		return fdo;
	}
	status = copy_fd(fdi, fdo);
	switch (status) {
	case COPY_READ_ERROR:
		error_errno("copy-fd: read returned");
		break;
	case COPY_WRITE_ERROR:
		error_errno("copy-fd: write returned");
		break;
	}
	close(fdi);
	if (close(fdo) != 0)
		return error_errno("%s: close error", dst);

	if (!status && adjust_shared_perm(repo, dst))
		return -1;

	return status;
}

int copy_file_with_time(struct repository *repo,
			const char *dst, const char *src, int mode)
{
	int status = copy_file(repo, dst, src, mode);
	if (!status)
		return copy_times(dst, src);
	return status;
}

int reflink_file(const char *dst, const char *src, int mode)
{
#if defined(__APPLE__)
	/*
	 * clonefile() refuses to operate when the destination exists and
	 * copies the source's permissions for us, so "mode" is unused here.
	 */
	(void)mode;
	if (clonefile(src, dst, 0) < 0)
		return -1;
	if (adjust_shared_perm(the_repository, dst))
		return -1;
	return 0;
#elif defined(__linux__)
	int fdi, fdo, status;

	mode = (mode & 0111) ? 0777 : 0666;
	if ((fdi = open(src, O_RDONLY)) < 0)
		return -1;
	if ((fdo = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode)) < 0) {
		int saved = errno;
		close(fdi);
		errno = saved;
		return -1;
	}
	status = ioctl(fdo, FICLONE, fdi);
	close(fdi);
	if (status < 0) {
		int saved = errno;
		close(fdo);
		/* we created an empty file above; do not leave it behind */
		unlink(dst);
		errno = saved;
		return -1;
	}
	if (close(fdo) != 0)
		return -1;
	if (adjust_shared_perm(the_repository, dst))
		return -1;
	return 0;
#else
	/* No reflink support on this platform (e.g. Windows). */
	(void)dst;
	(void)src;
	(void)mode;
	errno = ENOSYS;
	return -1;
#endif
}
