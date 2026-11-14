#ifndef COMPAT_MSVC_POSIX_H
#define COMPAT_MSVC_POSIX_H

#include <direct.h>
#include <process.h>
#include <malloc.h>
#include <io.h>

#pragma warning(disable: 4018) /* signed/unsigned comparison */
#pragma warning(disable: 4244) /* type conversion, possible loss of data */
#pragma warning(disable: 4090) /* 'function' : different 'const' qualifiers (ALLOC_GROW etc.)*/

/* porting function */
#define inline __inline
#define __inline__ __inline
#define __attribute__(x)
#define strcasecmp   _stricmp
#define strncasecmp  _strnicmp
#define strtoull     _strtoui64
#define strtoll      _strtoi64

#undef ERROR

#define ftello _ftelli64

typedef int sigset_t;
/* open for reading, writing, or both (not in fcntl.h) */
#define O_ACCMODE     (_O_RDONLY | _O_WRONLY | _O_RDWR)

#include "mingw-posix.h"

/*
 * MSVC's `_chsize()` takes a 32-bit `long` and silently truncates files
 * to 2 GiB. `_chsize_s()` accepts a 64-bit length but returns 0 on
 * success or an errno value on failure, rather than the -1/errno
 * convention POSIX `ftruncate()` callers expect. Wrap it so callers
 * that test the return value as `< 0` or against `-1` keep working.
 *
 * Note: this declaration must follow `#include "mingw-posix.h"` so
 * `off_t` resolves to `off64_t` and the parameter type matches the
 * underlying `_chsize_s()` width.
 */
static inline int msvc_ftruncate(int fd, off_t length)
{
	int err = _chsize_s(fd, length);

	if (err) {
		errno = err;
		return -1;
	}
	return 0;
}
#define ftruncate msvc_ftruncate

#endif /* COMPAT_MSVC_POSIX_H */
