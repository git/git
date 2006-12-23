#include "cache.h"

void read_or_die(int fd, void *buf, size_t count)
{
	char *p = buf;
	ssize_t loaded;

	while (count > 0) {
		loaded = xread(fd, p, count);
		if (loaded == 0)
			die("unexpected end of file");
		else if (loaded < 0)
			die("read error (%s)", strerror(errno));
		count -= loaded;
		p += loaded;
	}
}

void write_or_die(int fd, const void *buf, size_t count)
{
	const char *p = buf;
	ssize_t written;

	while (count > 0) {
		written = xwrite(fd, p, count);
		if (written == 0)
			die("disk full?");
		else if (written < 0) {
			if (errno == EPIPE)
				exit(0);
			die("write error (%s)", strerror(errno));
		}
		count -= written;
		p += written;
	}
}

int write_or_whine(int fd, const void *buf, size_t count, const char *msg)
{
	const char *p = buf;
	ssize_t written;

	while (count > 0) {
		written = xwrite(fd, p, count);
		if (written == 0) {
			fprintf(stderr, "%s: disk full?\n", msg);
			return 0;
		}
		else if (written < 0) {
			if (errno == EPIPE)
				exit(0);
			fprintf(stderr, "%s: write error (%s)\n",
				msg, strerror(errno));
			return 0;
		}
		count -= written;
		p += written;
	}

	return 1;
}
