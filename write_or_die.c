#include "cache.h"

/*
 * Some cases use stdio, but want to flush after the write
 * to get error handling (and to get better interactive
 * behaviour - not buffering excessively).
 *
 * Of course, if the flush happened within the write itself,
 * we've already lost the error code, and cannot report it any
 * more. So we just ignore that case instead (and hope we get
 * the right error code on the flush).
 *
 * If the file handle is stdout, and stdout is a file, then skip the
 * flush entirely since it's not needed.
 */
void maybe_flush_or_die(FILE *f, const char *desc)
{
	static int skip_stdout_flush = -1;
	struct stat st;
	char *cp;

	if (f == stdout) {
		if (skip_stdout_flush < 0) {
			cp = getenv("GIT_FLUSH");
			if (cp)
				skip_stdout_flush = (atoi(cp) == 0);
			else if ((fstat(fileno(stdout), &st) == 0) &&
				 S_ISREG(st.st_mode))
				skip_stdout_flush = 1;
			else
				skip_stdout_flush = 0;
		}
		if (skip_stdout_flush && !ferror(f))
			return;
	}
	if (fflush(f)) {
		if (errno == EPIPE)
			exit(0);
		die("write failure on %s: %s", desc, strerror(errno));
	}
}

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

int write_in_full(int fd, const void *buf, size_t count)
{
	const char *p = buf;
	ssize_t total = 0;

	while (count > 0) {
		ssize_t written = xwrite(fd, p, count);
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
