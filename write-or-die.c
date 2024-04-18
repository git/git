#include "git-compat-util.h"
#include "parse.h"
#include "run-command.h"
#include "write-or-die.h"

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
	if (f == stdout) {
		static int force_flush_stdout = -1;

		if (force_flush_stdout < 0) {
			force_flush_stdout = git_env_bool("GIT_FLUSH", -1);
			if (force_flush_stdout < 0) {
				struct stat st;
				if (fstat(fileno(stdout), &st))
					force_flush_stdout = 1;
				else
					force_flush_stdout = !S_ISREG(st.st_mode);
			}
		}
		if (!force_flush_stdout && !ferror(f))
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

static int maybe_fsync(int fd)
{
	if (use_fsync < 0)
		use_fsync = git_env_bool("GIT_TEST_FSYNC", 1);
	if (!use_fsync)
		return 0;

	if (fsync_method == FSYNC_METHOD_WRITEOUT_ONLY &&
	    git_fsync(fd, FSYNC_WRITEOUT_ONLY) >= 0)
		return 0;

	return git_fsync(fd, FSYNC_HARDWARE_FLUSH);
}

void fsync_or_die(int fd, const char *msg)
{
	if (maybe_fsync(fd) < 0)
		die_errno("fsync error on '%s'", msg);
}

int fsync_component(enum fsync_component component, int fd)
{
	if (fsync_components & component)
		return maybe_fsync(fd);
	return 0;
}

void fsync_component_or_die(enum fsync_component component, int fd, const char *msg)
{
	if (fsync_components & component)
		fsync_or_die(fd, msg);
}

void write_or_die(int fd, const void *buf, size_t count)
{
	if (write_in_full(fd, buf, count) < 0) {
		check_pipe(errno);
		die_errno("write error");
	}
}

void fwrite_or_die(FILE *f, const void *buf, size_t count)
{
	if (fwrite(buf, 1, count, f) != count)
		die_errno("fwrite error");
}

void fflush_or_die(FILE *f)
{
	if (fflush(f))
		die_errno("fflush error");
}
