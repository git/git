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

#elif defined(GIT_WINDOWS_NATIVE)

#include "win32.h"

int enable_pipe_nonblock(int fd)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	DWORD mode;
	DWORD type = GetFileType(h);
	if (type == FILE_TYPE_UNKNOWN && GetLastError() != NO_ERROR) {
		errno = EBADF;
		return -1;
	}
	if (type != FILE_TYPE_PIPE)
		BUG("unsupported file type: %lu", type);
	if (!GetNamedPipeHandleState(h, &mode, NULL, NULL, NULL, NULL, 0)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	mode |= PIPE_NOWAIT;
	if (!SetNamedPipeHandleState(h, &mode, NULL, NULL)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}
	return 0;
}

#else

int enable_pipe_nonblock(int fd UNUSED)
{
	errno = ENOSYS;
	return -1;
}

#endif
