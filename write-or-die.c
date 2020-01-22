#include "cache.h"
#include "run-command.h"

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
		check_pipe(errno);
		die_errno("write failure on '%s'", desc);
	}
}

void fprintf_or_die(FILE *f, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vfprintf(f, fmt, ap);
	va_end(ap);

	if (ret < 0) {
		check_pipe(errno);
		die_errno("write error");
	}
}

void fsync_or_die(int fd, const char *msg)
{
	if (fsync(fd) < 0) {
		die_errno("fsync error on '%s'", msg);
	}
}

void write_or_die(int fd, const void *buf, size_t count)
{
	if (write_in_full(fd, buf, count) < 0) {
		check_pipe(errno);
		die_errno("write error");
	}
}
