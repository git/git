#define _POSIX_C_SOURCE 200112L
#include <sys/stat.h>  /* *stat, S_IS* */
#include <sys/types.h> /* mode_t       */

static inline mode_t mode_native_to_git(mode_t native_mode)
{
	mode_t perm_bits = native_mode & 07777;
	if (S_ISREG(native_mode))
		return 0100000 | perm_bits;
	if (S_ISDIR(native_mode))
		return 0040000 | perm_bits;
	if (S_ISLNK(native_mode))
		return 0120000 | perm_bits;
	if (S_ISBLK(native_mode))
		return 0060000 | perm_bits;
	if (S_ISCHR(native_mode))
		return 0020000 | perm_bits;
	if (S_ISFIFO(native_mode))
		return 0010000 | perm_bits;
	if (S_ISSOCK(native_mode))
		return 0140000 | perm_bits;
	/* Non-standard type bits were given. */
	return perm_bits;
}

int git_stat(const char *path, struct stat *buf)
{
	int rc = stat(path, buf);
	if (rc == 0)
		buf->st_mode = mode_native_to_git(buf->st_mode);
	return rc;
}

int git_fstat(int fd, struct stat *buf)
{
	int rc = fstat(fd, buf);
	if (rc == 0)
		buf->st_mode = mode_native_to_git(buf->st_mode);
	return rc;
}

int git_lstat(const char *path, struct stat *buf)
{
	int rc = lstat(path, buf);
	if (rc == 0)
		buf->st_mode = mode_native_to_git(buf->st_mode);
	return rc;
}
