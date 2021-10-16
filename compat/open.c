#include "git-compat-util.h"

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
