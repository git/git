#ifndef COMPAT_ZLIB_H
#define COMPAT_ZLIB_H

#include <zlib.h>

#if defined(NO_DEFLATE_BOUND) || ZLIB_VERNUM < 0x1200
# define deflateBound(c,s)  ((s) + (((s) + 7) >> 3) + (((s) + 63) >> 6) + 11)
#endif

#if ZLIB_VERNUM < 0x1221
struct gz_header_s {
	int os;
};

static int deflateSetHeader(z_streamp strm, struct gz_header_s *head)
{
	(void)(strm);
	(void)(head);
	return Z_OK;
}
#endif

#endif /* COMPAT_ZLIB_H */
