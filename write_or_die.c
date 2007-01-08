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

int write_in_full(int fd, const void *buf, size_t count)
{
	const char *p = buf;
	ssize_t total = 0;
	ssize_t wcount = 0;

	while (count > 0) {
		wcount = xwrite(fd, p, count);
		if (wcount <= 0) {
			if (total)
				return total;
			else
				return wcount;
		}
		count -= wcount;
		p += wcount;
		total += wcount;
	}

	return wcount;
}

int write_or_whine_pipe(int fd, const void *buf, size_t count, const char *msg)
{
	ssize_t written;

	written = write_in_full(fd, buf, count);
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

	return 1;
}

int write_or_whine(int fd, const void *buf, size_t count, const char *msg)
{
	ssize_t written;

	written = write_in_full(fd, buf, count);
	if (written == 0) {
		fprintf(stderr, "%s: disk full?\n", msg);
		return 0;
	}
	else if (written < 0) {
		fprintf(stderr, "%s: write error (%s)\n",
			msg, strerror(errno));
		return 0;
	}

	return 1;
}
