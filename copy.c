#include "cache.h"

int copy_fd(int ifd, int ofd)
{
	while (1) {
		int len;
		char buffer[8192];
		char *buf = buffer;
		len = read(ifd, buffer, sizeof(buffer));
		if (!len)
			break;
		if (len < 0) {
			int read_error;
			if (errno == EAGAIN)
				continue;
			read_error = errno;
			close(ifd);
			return error("copy-fd: read returned %s",
				     strerror(read_error));
		}
		while (1) {
			int written = write(ofd, buf, len);
			if (written > 0) {
				buf += written;
				len -= written;
				if (!len)
					break;
			}
			if (!written)
				return error("copy-fd: write returned 0");
			if (errno == EAGAIN || errno == EINTR)
				continue;
			return error("copy-fd: write returned %s",
				     strerror(errno));
		}
	}
	close(ifd);
	return 0;
}

