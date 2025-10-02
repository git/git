#ifndef COMPAT_ZLIB_H
#define COMPAT_ZLIB_H

#ifdef HAVE_ZLIB_NG
# include <zlib-ng.h>

# define z_stream_s zng_stream_s
# define gz_header_s zng_gz_header_s

# define crc32(crc, buf, len) zng_crc32(crc, buf, len)

# define inflate(strm, bits) zng_inflate(strm, bits)
# define inflateEnd(strm) zng_inflateEnd(strm)
# define inflateInit(strm) zng_inflateInit(strm)
# define inflateInit2(strm, bits) zng_inflateInit2(strm, bits)
# define inflateReset(strm) zng_inflateReset(strm)

# define deflate(strm, flush) zng_deflate(strm, flush)
# define deflateBound(strm, source_len) zng_deflateBound(strm, source_len)
# define deflateEnd(strm) zng_deflateEnd(strm)
# define deflateInit(strm, level) zng_deflateInit(strm, level)
# define deflateInit2(stream, level, method, window_bits, mem_level, strategy) zng_deflateInit2(stream, level, method, window_bits, mem_level, strategy)
# define deflateReset(strm) zng_deflateReset(strm)
# define deflateSetHeader(strm, head) zng_deflateSetHeader(strm, head)

#else
# include <zlib.h>

# if defined(NO_DEFLATE_BOUND) || ZLIB_VERNUM < 0x1200
#  define deflateBound(c,s)  ((s) + (((s) + 7) >> 3) + (((s) + 63) >> 6) + 11)
# endif

/*
 * zlib only gained support for setting up the gzip header in v1.2.2.1. In
 * Git we only set the header to make archives reproducible across different
 * operating systems, so it's fine to simply make this a no-op when using a
 * zlib version that doesn't support this yet.
 */
# if ZLIB_VERNUM < 0x1221
struct gz_header_s {
	int os;
};

static int deflateSetHeader(z_streamp strm, struct gz_header_s *head)
{
	(void)(strm);
	(void)(head);
	return Z_OK;
}
# endif
#endif /* HAVE_ZLIB_NG */

#endif /* COMPAT_ZLIB_H */
