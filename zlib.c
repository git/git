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
