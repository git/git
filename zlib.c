/*
 * zlib wrappers to make sure we don't silently miss errors
 * at init time.
 */
#include "cache.h"

static const char *zerr_to_string(int status)
{
	switch (status) {
	case Z_MEM_ERROR:
		return "out of memory";
	case Z_VERSION_ERROR:
		return "wrong version";
	case Z_NEED_DICT:
		return "needs dictionary";
	case Z_DATA_ERROR:
		return "data stream error";
	case Z_STREAM_ERROR:
		return "stream consistency error";
	default:
		return "unknown error";
	}
}

void git_inflate_init(z_streamp strm)
{
	int status = inflateInit(strm);

	if (status == Z_OK)
		return;
	die("inflateInit: %s (%s)", zerr_to_string(status),
	    strm->msg ? strm->msg : "no message");
}

void git_inflate_init_gzip_only(z_streamp strm)
{
	/*
	 * Use default 15 bits, +16 is to accept only gzip and to
	 * yield Z_DATA_ERROR when fed zlib format.
	 */
	const int windowBits = 15 + 16;
	int status = inflateInit2(strm, windowBits);

	if (status == Z_OK)
		return;
	die("inflateInit2: %s (%s)", zerr_to_string(status),
	    strm->msg ? strm->msg : "no message");
}

void git_inflate_end(z_streamp strm)
{
	int status = inflateEnd(strm);

	if (status == Z_OK)
		return;
	error("inflateEnd: %s (%s)", zerr_to_string(status),
	      strm->msg ? strm->msg : "no message");
}

int git_inflate(z_streamp strm, int flush)
{
	int status = inflate(strm, flush);

	switch (status) {
	/* Z_BUF_ERROR: normal, needs more space in the output buffer */
	case Z_BUF_ERROR:
	case Z_OK:
	case Z_STREAM_END:
		return status;

	case Z_MEM_ERROR:
		die("inflate: out of memory");
	default:
		break;
	}
	error("inflate: %s (%s)", zerr_to_string(status),
	      strm->msg ? strm->msg : "no message");
	return status;
}

#if defined(NO_DEFLATE_BOUND) || ZLIB_VERNUM < 0x1200
#define deflateBound(c,s)  ((s) + (((s) + 7) >> 3) + (((s) + 63) >> 6) + 11)
#endif

unsigned long git_deflate_bound(z_streamp strm, unsigned long size)
{
	return deflateBound(strm, size);
}

void git_deflate_init(z_streamp strm, int level)
{
	int status = deflateInit(strm, level);

	if (status == Z_OK)
		return;
	die("deflateInit: %s (%s)", zerr_to_string(status),
	    strm->msg ? strm->msg : "no message");
}

void git_deflate_init_gzip(z_streamp strm, int level)
{
	/*
	 * Use default 15 bits, +16 is to generate gzip header/trailer
	 * instead of the zlib wrapper.
	 */
	const int windowBits = 15 + 16;
	int status = deflateInit2(strm, level,
				  Z_DEFLATED, windowBits,
				  8, Z_DEFAULT_STRATEGY);
	if (status == Z_OK)
		return;
	die("deflateInit2: %s (%s)", zerr_to_string(status),
	    strm->msg ? strm->msg : "no message");
}

void git_deflate_end(z_streamp strm)
{
	int status = deflateEnd(strm);

	if (status == Z_OK)
		return;
	error("deflateEnd: %s (%s)", zerr_to_string(status),
	      strm->msg ? strm->msg : "no message");
}

int git_deflate_end_gently(z_streamp strm)
{
	return deflateEnd(strm);
}

int git_deflate(z_streamp strm, int flush)
{
	int status = deflate(strm, flush);

	switch (status) {
	/* Z_BUF_ERROR: normal, needs more space in the output buffer */
	case Z_BUF_ERROR:
	case Z_OK:
	case Z_STREAM_END:
		return status;

	case Z_MEM_ERROR:
		die("deflate: out of memory");
	default:
		break;
	}
	error("deflate: %s (%s)", zerr_to_string(status),
	      strm->msg ? strm->msg : "no message");
	return status;
}
