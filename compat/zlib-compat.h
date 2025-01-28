#ifndef COMPAT_ZLIB_H
#define COMPAT_ZLIB_H

#include <zlib.h>

#if defined(NO_DEFLATE_BOUND) || ZLIB_VERNUM < 0x1200
# define deflateBound(c,s)  ((s) + (((s) + 7) >> 3) + (((s) + 63) >> 6) + 11)
#endif

#endif /* COMPAT_ZLIB_H */
