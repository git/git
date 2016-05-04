#ifndef __MSVC__HEAD
#define __MSVC__HEAD

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
#define strncasecmp  _strnicmp
#define ftruncate    _chsize
#define strtoull     _strtoui64
#define strtoll      _strtoi64

static __inline int strcasecmp (const char *s1, const char *s2)
{
	int size1 = strlen(s1);
	int sisz2 = strlen(s2);
	return _strnicmp(s1, s2, sisz2 > size1 ? sisz2 : size1);
}

#undef ERROR

#define ftello _ftelli64

typedef int sigset_t;
/* open for reading, writing, or both (not in fcntl.h) */
#define O_ACCMODE     (_O_RDONLY | _O_WRONLY | _O_RDWR)

#include "compat/mingw.h"

#endif
