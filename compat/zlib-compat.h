#ifndef COMPAT_ZLIB_H
#define COMPAT_ZLIB_H

#include <zlib.h>

#if defined(NO_DEFLATE_BOUND) || ZLIB_VERNUM < 0x1200
# define deflateBound(c,s)  ((s) + (((s) + 7) >> 3) + (((s) + 63) >> 6) + 11)
#endif

/*
 * zlib only gained support for setting up the gzip header in v1.2.2.1. In
 * Git we only set the header to make archives reproducible across different
 * operating systems, so it's fine to simply make this a no-op when using a
 * zlib version that doesn't support this yet.
 */
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
