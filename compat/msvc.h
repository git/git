#ifndef __MSVC__HEAD
#define __MSVC__HEAD

#include <direct.h>
#include <process.h>
#include <malloc.h>
#include <io.h>

/* porting function */
#define inline __inline
#define __inline__ __inline
#define __attribute__(x)
#define strcasecmp   _stricmp
#define strncasecmp  _strnicmp
#define ftruncate    _chsize
#define strtoull     _strtoui64
#define strtoll      _strtoi64

#undef ERROR

#include "compat/mingw.h"

#endif
