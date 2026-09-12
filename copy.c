#include "git-compat-util.h"
#include "copy.h"
#include "path.h"
#include "gettext.h"
#include "strbuf.h"
#include "abspath.h"

#ifdef __linux__
#include <sys/ioctl.h>

#define FICLONE _IOW(0x94, 9, int)
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

static int finish_copy(struct repository *repo, const char *dst,
		       int fdi, int fdo, int status)
{
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

int copy_file_reflink(struct repository *repo,
		       const char *dst, const char *src, int mode)
{
#ifndef FICLONE
	(void)repo;
	(void)dst;
	(void)src;
	(void)mode;
	errno = ENOTSUP;
	return -1;
#else
	int fdi, fdo, status;

	mode = (mode & 0111) ? 0777 : 0666;
	if ((fdi = open(src, O_RDONLY)) < 0)
		return fdi;
	if ((fdo = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode)) < 0) {
		close(fdi);
		return fdo;
	}
	status = ioctl(fdo, FICLONE, fdi);
	if (status) {
		int saved_errno = errno;

		close(fdi);
		close(fdo);
		unlink(dst);
		errno = saved_errno;
		return -1;
	}

	return finish_copy(repo, dst, fdi, fdo, 0);
#endif
}

int copy_file_reflink_with_time(struct repository *repo,
			 const char *dst, const char *src, int mode)
{
	int saved_errno;

	if (copy_file_reflink(repo, dst, src, mode))
		return -1;
	if (!copy_times(dst, src))
		return 0;

	saved_errno = errno;
	unlink(dst);
	errno = saved_errno;
	return -1;
}

static int copy_file_contents(struct repository *repo,
			      const char *dst, const char *src, int mode)
{
	int fdi, fdo;

	mode = (mode & 0111) ? 0777 : 0666;
	if ((fdi = open(src, O_RDONLY)) < 0)
		return fdi;
	if ((fdo = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode)) < 0) {
		close(fdi);
		return fdo;
	}

	return finish_copy(repo, dst, fdi, fdo, copy_fd(fdi, fdo));
}

int copy_file(struct repository *repo,
	      const char *dst, const char *src, int mode)
{
	if (!copy_file_reflink(repo, dst, src, mode))
		return 0;

	return copy_file_contents(repo, dst, src, mode);
}

int copy_file_with_time(struct repository *repo,
			const char *dst, const char *src, int mode)
{
	int status = copy_file(repo, dst, src, mode);
	if (!status)
		return copy_times(dst, src);
	return status;
}
