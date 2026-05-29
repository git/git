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
#elif defined(GIT_WINDOWS_NATIVE)
#include <winioctl.h>
/*
 * Block cloning on ReFS uses FSCTL_DUPLICATE_EXTENTS_TO_FILE. Older Windows
 * SDK headers may lack the control code and its payload struct, so define
 * them ourselves when missing (the values are part of the stable Win32 API).
 */
#ifndef FSCTL_DUPLICATE_EXTENTS_TO_FILE
#define FSCTL_DUPLICATE_EXTENTS_TO_FILE \
	CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 209, METHOD_BUFFERED, FILE_WRITE_DATA)
typedef struct _DUPLICATE_EXTENTS_DATA {
	HANDLE FileHandle;
	LARGE_INTEGER SourceFileOffset;
	LARGE_INTEGER TargetFileOffset;
	LARGE_INTEGER ByteCount;
} DUPLICATE_EXTENTS_DATA, *PDUPLICATE_EXTENTS_DATA;
#endif
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

#ifdef GIT_WINDOWS_NATIVE
/*
 * Clone [offset, offset+count) of "src" onto "dst" via ReFS block cloning.
 * "count" is rounded up to a cluster boundary by the caller; a range that
 * runs past the source's end of file is harmless and ignored by Windows.
 */
static int clone_extent_windows(HANDLE dst, HANDLE src,
				uint64_t offset, uint64_t count)
{
	DUPLICATE_EXTENTS_DATA dup;
	DWORD bytes;

	dup.FileHandle = src;
	dup.SourceFileOffset.QuadPart = offset;
	dup.TargetFileOffset.QuadPart = offset;
	dup.ByteCount.QuadPart = count;
	if (!DeviceIoControl(dst, FSCTL_DUPLICATE_EXTENTS_TO_FILE,
			     &dup, sizeof(dup), NULL, 0, &bytes, NULL))
		return -1;
	return 0;
}
#endif

int reflink_file(struct repository *repo,
		 const char *dst, const char *src, int mode)
{
#if defined(__APPLE__)
	/*
	 * clonefile() refuses to operate when the destination exists and
	 * copies the source's permissions for us, so "mode" is unused here.
	 */
	(void)mode;
	if (clonefile(src, dst, 0) < 0)
		return -1;
	if (adjust_shared_perm(repo, dst))
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
	if (adjust_shared_perm(repo, dst))
		return -1;
	return 0;
#elif defined(GIT_WINDOWS_NATIVE)
	/*
	 * ReFS supports copy-on-write block cloning via
	 * FSCTL_DUPLICATE_EXTENTS_TO_FILE, the same mechanism Git LFS uses.
	 * Cloned ranges must start and end on a cluster boundary (ReFS uses
	 * 64K or 4K clusters) and span less than 4GiB, so clone in <=1GiB
	 * chunks and round the final chunk up to a cluster; the destination is
	 * sized to the source's exact length and an over-long final range past
	 * the source's EOF is ignored.
	 */
	const uint64_t GiB = (uint64_t)1 << 30;
	const uint64_t clusters[] = { 64 * 1024, 4 * 1024 };
	int fdi, fdo, ret = -1;
	HANDLE hi, ho;
	LARGE_INTEGER size;
	uint64_t off;
	size_t i;

	mode = (mode & 0111) ? 0777 : 0666;
	if ((fdi = open(src, O_RDONLY | O_BINARY)) < 0)
		return -1;
	if ((fdo = open(dst, O_RDWR | O_CREAT | O_EXCL | O_BINARY, mode)) < 0) {
		int saved = errno;
		close(fdi);
		errno = saved;
		return -1;
	}
	hi = (HANDLE)_get_osfhandle(fdi);
	ho = (HANDLE)_get_osfhandle(fdo);
	if (hi == INVALID_HANDLE_VALUE || ho == INVALID_HANDLE_VALUE ||
	    !GetFileSizeEx(hi, &size)) {
		errno = EINVAL;
		goto cleanup;
	}

	/* the destination must be the source's exact size before cloning */
	if (!SetFilePointerEx(ho, size, NULL, FILE_BEGIN) || !SetEndOfFile(ho)) {
		errno = EIO;
		goto cleanup;
	}

	ret = 0;
	/* clone whole 1GiB regions (already cluster aligned) */
	for (off = 0; (uint64_t)size.QuadPart > off &&
		      (uint64_t)size.QuadPart - off > GiB; off += GiB) {
		if (clone_extent_windows(ho, hi, off, GiB)) {
			errno = ENOSYS;
			ret = -1;
			goto cleanup;
		}
	}
	/* clone the tail, rounding its length up to a cluster (64K then 4K) */
	if (!ret && (uint64_t)size.QuadPart > off) {
		uint64_t rem = (uint64_t)size.QuadPart - off;

		ret = -1;
		for (i = 0; i < ARRAY_SIZE(clusters); i++) {
			uint64_t count = (rem + clusters[i] - 1) &
					 ~(clusters[i] - 1);
			if (!clone_extent_windows(ho, hi, off, count)) {
				ret = 0;
				break;
			}
		}
		if (ret)
			errno = ENOSYS;	/* not a CoW volume */
	}

cleanup:
	close(fdi);
	if (close(fdo) && !ret) {
		errno = EIO;
		ret = -1;
	}
	if (ret) {
		int saved = errno;
		unlink(dst);
		errno = saved;
		return -1;
	}
	if (adjust_shared_perm(repo, dst))
		return -1;
	return 0;
#else
	/* No reflink primitive on this platform. */
	(void)repo;
	(void)dst;
	(void)src;
	(void)mode;
	errno = ENOSYS;
	return -1;
#endif
}
