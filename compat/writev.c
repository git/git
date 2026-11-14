#include "../git-compat-util.h"
#include "../wrapper.h"

ssize_t git_writev(int fd, const struct iovec *iov, int iovcnt)
{
	size_t sum = 0;

	if (iovcnt <= 0) {
		errno = EINVAL;
		return -1;
	}

	/*
	 * According to writev(3p), the syscall shall error with EINVAL in case
	 * the sum of `iov_len` overflows `ssize_t`.
	 */
	for (int i = 0; i < iovcnt; i++) {
		if (iov[i].iov_len > maximum_signed_value_of_type(ssize_t) ||
		    unsigned_add_overflows(iov[i].iov_len, sum) ||
		    iov[i].iov_len + sum > maximum_signed_value_of_type(ssize_t)) {
			errno = EINVAL;
			return -1;
		}

		sum += iov[i].iov_len;
	}

	/*
	 * We only ever write the first non-empty vector so that we can
	 * guarantee the call to be non-interleaving as guaranteed by POSIX.
	 * This works just fine as callers have to loop around writev anyway.
	 */
	for (int i = 0; i < iovcnt; i++) {
		if (!iov[i].iov_len)
			continue;
		return xwrite(fd, iov[i].iov_base, iov[i].iov_len);
	}

	/* When all iovec members were zero we ought to return 0 according to POSIX. */
	return 0;
}
