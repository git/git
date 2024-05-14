#include "git-compat-util.h"
#include "copy.h"
#include "path.h"
#include "gettext.h"
#include "strbuf.h"
#include "abspath.h"

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

int copy_file(const char *dst, const char *src, int mode)
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

	if (!status && adjust_shared_perm(dst))
		return -1;

	return status;
}

int copy_file_with_time(const char *dst, const char *src, int mode)
{
	int status = copy_file(dst, src, mode);
	if (!status)
		return copy_times(dst, src);
	return status;
}

static int do_symlinks_match(const char *path1, const char *path2)
{
	struct strbuf buf1 = STRBUF_INIT, buf2 = STRBUF_INIT;
	int ret = 0;

	if (!strbuf_readlink(&buf1, path1, 0) &&
	    !strbuf_readlink(&buf2, path2, 0))
		ret = !strcmp(buf1.buf, buf2.buf);

	strbuf_release(&buf1);
	strbuf_release(&buf2);
	return ret;
}

int do_files_match(const char *path1, const char *path2)
{
	struct stat st1, st2;
	int fd1 = -1, fd2 = -1, ret = 1;
	char buf1[8192], buf2[8192];

	if ((fd1 = open_nofollow(path1, O_RDONLY)) < 0 ||
	    fstat(fd1, &st1) || !S_ISREG(st1.st_mode)) {
		if (fd1 < 0 && errno == ELOOP)
			/* maybe this is a symbolic link? */
			return do_symlinks_match(path1, path2);
		ret = 0;
	} else if ((fd2 = open_nofollow(path2, O_RDONLY)) < 0 ||
		   fstat(fd2, &st2) || !S_ISREG(st2.st_mode)) {
		ret = 0;
	}

	if (ret)
		/* to match, neither must be executable, or both */
		ret = !(st1.st_mode & 0111) == !(st2.st_mode & 0111);

	if (ret)
		ret = st1.st_size == st2.st_size;

	while (ret) {
		ssize_t len1 = read_in_full(fd1, buf1, sizeof(buf1));
		ssize_t len2 = read_in_full(fd2, buf2, sizeof(buf2));

		if (len1 < 0 || len2 < 0 || len1 != len2)
			ret = 0; /* read error or different file size */
		else if (!len1) /* len2 is also 0; hit EOF on both */
			break; /* ret is still true */
		else
			ret = !memcmp(buf1, buf2, len1);
	}

	if (fd1 >= 0)
		close(fd1);
	if (fd2 >= 0)
		close(fd2);

	return ret;
}
