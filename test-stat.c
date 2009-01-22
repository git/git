#include <sys/stat.h>
#include <time.h>
#include <stdio.h>

#ifdef EMULATE
#define HINT fprintf(stderr, "emulated stat\n")
#include <errno.h>
#include <windows.h>
#include <unistd.h>

/* Use mingw_lstat() instead of lstat()/stat() and
 * mingw_fstat() instead of fstat() on Windows.
 */
int mingw_lstat(const char *file_name, struct stat *buf);
int mingw_fstat(int fd, struct stat *buf);
#define fstat mingw_fstat
#define lstat mingw_lstat
#define stat(x,y) mingw_lstat(x,y)

#define PATH_MAX 260

static inline int file_attr_to_st_mode (DWORD attr)
{
	int fMode = S_IREAD;
	if (attr & FILE_ATTRIBUTE_DIRECTORY)
		fMode |= S_IFDIR;
	else
		fMode |= S_IFREG;
	if (!(attr & FILE_ATTRIBUTE_READONLY))
		fMode |= S_IWRITE;
	return fMode;
}

static inline int get_file_attr(const char *fname, WIN32_FILE_ATTRIBUTE_DATA *fdata)
{
	if (GetFileAttributesExA(fname, GetFileExInfoStandard, fdata))
		return 0;

	switch (GetLastError()) {
	case ERROR_ACCESS_DENIED:
	case ERROR_SHARING_VIOLATION:
	case ERROR_LOCK_VIOLATION:
	case ERROR_SHARING_BUFFER_EXCEEDED:
		return EACCES;
	case ERROR_BUFFER_OVERFLOW:
		return ENAMETOOLONG;
	case ERROR_NOT_ENOUGH_MEMORY:
		return ENOMEM;
	default:
		return ENOENT;
	}
}

static inline time_t filetime_to_time_t(const FILETIME *ft)
{
	long long winTime = ((long long)ft->dwHighDateTime << 32) + ft->dwLowDateTime;
	winTime -= 116444736000000000LL; /* Windows to Unix Epoch conversion */
	winTime /= 10000000;		 /* Nano to seconds resolution */
	return (time_t)winTime;
}

/* We keep the do_lstat code in a separate function to avoid recursion.
 * When a path ends with a slash, the stat will fail with ENOENT. In
 * this case, we strip the trailing slashes and stat again.
 */
static int do_lstat(const char *file_name, struct stat *buf)
{
	WIN32_FILE_ATTRIBUTE_DATA fdata;

	if (!(errno = get_file_attr(file_name, &fdata))) {
		buf->st_ino = 0;
		buf->st_gid = 0;
		buf->st_uid = 0;
		buf->st_nlink = 1;
		buf->st_mode = file_attr_to_st_mode(fdata.dwFileAttributes);
		buf->st_size = fdata.nFileSizeLow; /* Can't use nFileSizeHigh, since it's not a stat64 */
		buf->st_dev = buf->st_rdev = 0; /* not used by Git */
		buf->st_atime = filetime_to_time_t(&(fdata.ftLastAccessTime));
		buf->st_mtime = filetime_to_time_t(&(fdata.ftLastWriteTime));
		buf->st_ctime = filetime_to_time_t(&(fdata.ftCreationTime));
		return 0;
	}
	return -1;
}

/* We provide our own lstat/fstat functions, since the provided
 * lstat/fstat functions are so slow. These stat functions are
 * tailored for Git's usage (read: fast), and are not meant to be
 * complete. Note that Git stat()s are redirected to mingw_lstat()
 * too, since Windows doesn't really handle symlinks that well.
 */
int mingw_lstat(const char *file_name, struct stat *buf)
{
	int namelen;
	static char alt_name[PATH_MAX];

	if (!do_lstat(file_name, buf))
		return 0;

	/* if file_name ended in a '/', Windows returned ENOENT;
	 * try again without trailing slashes
	 */
	if (errno != ENOENT)
		return -1;

	namelen = strlen(file_name);
	if (namelen && file_name[namelen-1] != '/')
		return -1;
	while (namelen && file_name[namelen-1] == '/')
		--namelen;
	if (!namelen || namelen >= PATH_MAX)
		return -1;

	memcpy(alt_name, file_name, namelen);
	alt_name[namelen] = 0;
	return do_lstat(alt_name, buf);
}

#undef fstat
int mingw_fstat(int fd, struct stat *buf)
{
	HANDLE fh = (HANDLE)_get_osfhandle(fd);
	BY_HANDLE_FILE_INFORMATION fdata;

	if (fh == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}
	/* direct non-file handles to MS's fstat() */
	if (GetFileType(fh) != FILE_TYPE_DISK)
		return fstat(fd, buf);

	if (GetFileInformationByHandle(fh, &fdata)) {
		buf->st_ino = 0;
		buf->st_gid = 0;
		buf->st_uid = 0;
		buf->st_nlink = 1;
		buf->st_mode = file_attr_to_st_mode(fdata.dwFileAttributes);
		buf->st_size = fdata.nFileSizeLow; /* Can't use nFileSizeHigh, since it's not a stat64 */
		buf->st_dev = buf->st_rdev = 0; /* not used by Git */
		buf->st_atime = filetime_to_time_t(&(fdata.ftLastAccessTime));
		buf->st_mtime = filetime_to_time_t(&(fdata.ftLastWriteTime));
		buf->st_ctime = filetime_to_time_t(&(fdata.ftCreationTime));
		return 0;
	}
	errno = EBADF;
	return -1;
}

#else
#define HINT (void)0
#endif

int main(int argc, char**argv)
{
	int i;
	int err = 0;
	HINT;
	for (i = 1; i < argc; i++)
	{
		struct stat st;
		if (stat(argv[i], &st)) {
			perror(argv[i]);
			err = 1;
			continue;
		}
		printf("%s: %s\n", argv[i], ctime(&st.st_mtime));
	}
	return err;
}
