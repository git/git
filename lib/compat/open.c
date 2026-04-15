#include "git-compat-util.h"

#ifdef OPEN_RETURNS_EINTR
#undef open
int git_open_with_retry(const char *path, int flags, ...)
{
	mode_t mode = 0;
	int ret;

	/*
	 * Also O_TMPFILE would take a mode, but it isn't defined everywhere.
	 * And anyway, we don't use it in our code base.
	 */
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}

	do {
		ret = open(path, flags, mode);
	} while (ret < 0 && errno == EINTR);

	return ret;
}
#endif

int git_open_cloexec(const char *name, int flags)
{
	int fd;
	static int o_cloexec = O_CLOEXEC;

	fd = open(name, flags | o_cloexec);
	if ((o_cloexec & O_CLOEXEC) && fd < 0 && errno == EINVAL) {
		/* Try again w/o O_CLOEXEC: the kernel might not support it */
		o_cloexec &= ~O_CLOEXEC;
		fd = open(name, flags | o_cloexec);
	}

#if defined(F_GETFD) && defined(F_SETFD) && defined(FD_CLOEXEC)
	{
		static int fd_cloexec = FD_CLOEXEC;

		if (!o_cloexec && 0 <= fd && fd_cloexec) {
			/* Opened w/o O_CLOEXEC?  try with fcntl(2) to add it */
			int flags = fcntl(fd, F_GETFD);
			if (fcntl(fd, F_SETFD, flags | fd_cloexec))
				fd_cloexec = 0;
		}
	}
#endif
	return fd;
}
