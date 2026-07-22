#include "../git-compat-util.h"
#include "../wrapper.h"

ssize_t git_writev(int fd, const struct iovec *iov, int iovcnt)
{
	size_t total_written = 0;
	size_t sum = 0;

	/*
	 * According to writev(3p), the syscall shall error with EINVAL in case
	 * the sum of `iov_len` overflows `ssize_t`.
	 */
	 for (int i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len > maximum_signed_value_of_type(ssize_t) ||
		    iov[i].iov_len + sum > maximum_signed_value_of_type(ssize_t)) {
			errno = EINVAL;
			return -1;
		}

		sum += iov[i].iov_len;
	}

	for (int i = 0; i < iovcnt; i++) {
		const char *bytes = iov[i].iov_base;
		size_t iovec_written = 0;

		while (iovec_written < iov[i].iov_len) {
			ssize_t bytes_written = xwrite(fd, bytes + iovec_written,
						       iov[i].iov_len - iovec_written);
			if (bytes_written < 0) {
				if (total_written)
					goto out;
				return bytes_written;
			}
			if (!bytes_written)
				goto out;
			iovec_written += bytes_written;
			total_written += bytes_written;
		}
	}

out:
	return (ssize_t) total_written;
}
