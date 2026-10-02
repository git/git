#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define FICLONE _IOW(0x94, 9, int)

static int emulate_clone(int dst, int src)
{
	char buf[8192];
	off_t pos = 0;

	for (;;) {
		ssize_t nr = pread(src, buf, sizeof(buf), pos);
		if (nr < 0)
			return -1;
		if (!nr)
			return ftruncate(dst, pos);
		if (pwrite(dst, buf, nr, pos) != nr)
			return -1;
		pos += nr;
	}
}

static void log_clone_attempt(void)
{
	const char *path = getenv("GIT_TEST_FICLONE_LOG");
	int fd;

	if (!path)
		return;
	fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd < 0)
		return;
	write(fd, "FICLONE\n", 8);
	close(fd);
}

int ioctl(int fd, unsigned long request, ...)
{
	static int (*real_ioctl)(int, unsigned long, ...);
	va_list ap;
	unsigned long arg;
	const char *mode;

	va_start(ap, request);
	arg = va_arg(ap, unsigned long);
	va_end(ap);

	if (request != FICLONE) {
		if (!real_ioctl)
			real_ioctl = dlsym(RTLD_NEXT, "ioctl");
		return real_ioctl(fd, request, arg);
	}

	log_clone_attempt();
	mode = getenv("GIT_TEST_FICLONE");
	if (!mode || !strcmp(mode, "real")) {
		if (!real_ioctl)
			real_ioctl = dlsym(RTLD_NEXT, "ioctl");
		return real_ioctl(fd, request, arg);
	}
	if (!strcmp(mode, "success"))
		return emulate_clone(fd, (int)arg);
	errno = !strcmp(mode, "unsupported") ? EOPNOTSUPP : EIO;
	return -1;
}

int link(const char *oldpath, const char *newpath)
{
	static int (*real_link)(const char *, const char *);

	if (!getenv("GIT_TEST_LINK_FAILURE")) {
		if (!real_link)
			real_link = dlsym(RTLD_NEXT, "link");
		return real_link(oldpath, newpath);
	}
	errno = EPERM;
	return -1;
}
