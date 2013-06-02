#include "../git-compat-util.h"
#undef write

/*
 * Version of write that will write at most INT_MAX bytes.
 * Workaround a xnu bug on Mac OS X
 */
ssize_t clipped_write(int fildes, const void *buf, size_t nbyte)
{
	if (nbyte > INT_MAX)
		nbyte = INT_MAX;
	return write(fildes, buf, nbyte);
}
