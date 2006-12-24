#include "../git-compat-util.h"

void *git_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
	int n = 0;
	off_t current_offset = lseek(fd, 0, SEEK_CUR);

	if (start != NULL || !(flags & MAP_PRIVATE))
		die("Invalid usage of mmap when built with NO_MMAP");

	if (lseek(fd, offset, SEEK_SET) < 0) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	start = xmalloc(length);
	if (start == NULL) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	while (n < length) {
		int count = read(fd, start+n, length-n);

		if (count == 0) {
			memset(start+n, 0, length-n);
			break;
		}

		if (count < 0) {
			free(start);
			errno = EACCES;
			return MAP_FAILED;
		}

		n += count;
	}

	if (current_offset != lseek(fd, current_offset, SEEK_SET)) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	return start;
}

int git_munmap(void *start, size_t length)
{
	free(start);
	return 0;
}

