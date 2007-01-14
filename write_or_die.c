#include "cache.h"

int read_in_full(int fd, void *buf, size_t count)
{
	char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t loaded = xread(fd, p, count);
		if (loaded <= 0)
			return total ? total : loaded;
		count -= loaded;
		p += loaded;
		total += loaded;
	}

	return total;
}

void read_or_die(int fd, void *buf, size_t count)
{
	ssize_t loaded;

	loaded = read_in_full(fd, buf, count);
	if (loaded != count) {
		if (loaded < 0)
			die("read error (%s)", strerror(errno));
		die("read error: end of file");
	}
}

int write_in_full(int fd, const void *buf, size_t count)
{
	const char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		size_t written = xwrite(fd, p, count);
		if (written < 0)
			return -1;
		if (!written) {
			errno = ENOSPC;
			return -1;
		}
		count -= written;
		p += written;
		total += written;
	}

	return total;
}

void write_or_die(int fd, const void *buf, size_t count)
{
	if (write_in_full(fd, buf, count) < 0) {
		if (errno == EPIPE)
			exit(0);
		die("write error (%s)", strerror(errno));
	}
}

int write_or_whine_pipe(int fd, const void *buf, size_t count, const char *msg)
{
	if (write_in_full(fd, buf, count) < 0) {
		if (errno == EPIPE)
			exit(0);
		fprintf(stderr, "%s: write error (%s)\n",
			msg, strerror(errno));
		return 0;
	}

	return 1;
}

int write_or_whine(int fd, const void *buf, size_t count, const char *msg)
{
	if (write_in_full(fd, buf, count) < 0) {
		fprintf(stderr, "%s: write error (%s)\n",
			msg, strerror(errno));
		return 0;
	}

	return 1;
}
