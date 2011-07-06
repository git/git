/*
 * zlib wrappers to make sure we don't silently miss errors
 * at init time.
 */
#include "cache.h"

void git_inflate_init(z_streamp strm)
{
	const char *err;

	switch (inflateInit(strm)) {
	case Z_OK:
		return;

	case Z_MEM_ERROR:
		err = "out of memory";
		break;
	case Z_VERSION_ERROR:
		err = "wrong version";
		break;
	default:
		err = "error";
	}
	die("inflateInit: %s (%s)", err, strm->msg ? strm->msg : "no message");
}

void git_inflate_end(z_streamp strm)
{
	if (inflateEnd(strm) != Z_OK)
		error("inflateEnd: %s", strm->msg ? strm->msg : "failed");
}

int git_inflate(z_streamp strm, int flush)
{
	int ret = inflate(strm, flush);
	const char *err;

	switch (ret) {
	/* Out of memory is fatal. */
	case Z_MEM_ERROR:
		die("inflate: out of memory");

	/* Data corruption errors: we may want to recover from them (fsck) */
	case Z_NEED_DICT:
		err = "needs dictionary"; break;
	case Z_DATA_ERROR:
		err = "data stream error"; break;
	case Z_STREAM_ERROR:
		err = "stream consistency error"; break;
	default:
		err = "unknown error"; break;

	/* Z_BUF_ERROR: normal, needs more space in the output buffer */
	case Z_BUF_ERROR:
	case Z_OK:
	case Z_STREAM_END:
		return ret;
	}
	error("inflate: %s (%s)", err, strm->msg ? strm->msg : "no message");
	return ret;
}
