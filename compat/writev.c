#include "../git-compat-util.h"
#include "../wrapper.h"

ssize_t git_writev(int fd, const struct iovec *iov, int iovcnt)
{
	size_t total_written = 0;

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
	return cast_size_t_to_ssize_t(total_written);
}
