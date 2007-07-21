#include "cache.h"

int copy_fd(int ifd, int ofd)
{
	while (1) {
		char buffer[8192];
		char *buf = buffer;
		ssize_t len = xread(ifd, buffer, sizeof(buffer));
		if (!len)
			break;
		if (len < 0) {
			int read_error;
			read_error = errno;
			close(ifd);
			return error("copy-fd: read returned %s",
				     strerror(read_error));
		}
		while (len) {
			int written = xwrite(ofd, buf, len);
			if (written > 0) {
				buf += written;
				len -= written;
			}
			else if (!written) {
				close(ifd);
				return error("copy-fd: write returned 0");
			} else {
				close(ifd);
				return error("copy-fd: write returned %s",
					     strerror(errno));
			}
		}
	}
	close(ifd);
	return 0;
}
