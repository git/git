#include "git-compat-util.h"
#include "nonblock.h"

#ifdef O_NONBLOCK

int enable_pipe_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return -1;
	flags |= O_NONBLOCK;
	return fcntl(fd, F_SETFL, flags);
}

#else

int enable_pipe_nonblock(int fd)
{
	errno = ENOSYS;
	return -1;
}

#endif
