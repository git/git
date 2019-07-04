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

/*
 * avail_in and avail_out in zlib are counted in uInt, which typically
 * limits the size of the buffer we can use to 4GB when interacting
 * with zlib in a single call to inflate/deflate.
 */
/* #define ZLIB_BUF_MAX ((uInt)-1) */
#define ZLIB_BUF_MAX ((uInt) 1024 * 1024 * 1024) /* 1GB */
static inline uInt zlib_buf_cap(size_t len)
{
	return (ZLIB_BUF_MAX < len) ? ZLIB_BUF_MAX : len;
}

static void zlib_pre_call(git_zstream *s)
{
	s->z.next_in = s->next_in;
	s->z.next_out = s->next_out;
	s->z.total_in = s->total_in;
	s->z.total_out = s->total_out;
	s->z.avail_in = zlib_buf_cap(s->avail_in);
	s->z.avail_out = zlib_buf_cap(s->avail_out);
}

static void zlib_post_call(git_zstream *s)
{
	size_t bytes_consumed;
	size_t bytes_produced;

	bytes_consumed = s->z.next_in - s->next_in;
	bytes_produced = s->z.next_out - s->next_out;
	if (s->z.total_out != s->total_out + bytes_produced)
		BUG("total_out mismatch");
	if (s->z.total_in != s->total_in + bytes_consumed)
		BUG("total_in mismatch");

	s->total_out = s->z.total_out;
	s->total_in = s->z.total_in;
	s->next_in = s->z.next_in;
	s->next_out = s->z.next_out;
	s->avail_in -= bytes_consumed;
	s->avail_out -= bytes_produced;
}

void git_inflate_init(git_zstream *strm)
{
	int status;

	zlib_pre_call(strm);
	status = inflateInit(&strm->z);
	zlib_post_call(strm);
	if (status == Z_OK)
		return;
	die("inflateInit: %s (%s)", zerr_to_string(status),
	    strm->z.msg ? strm->z.msg : "no message");
}

void git_inflate_init_gzip_only(git_zstream *strm)
{
	/*
	 * Use default 15 bits, +16 is to accept only gzip and to
	 * yield Z_DATA_ERROR when fed zlib format.
	 */
	const int windowBits = 15 + 16;
	int status;

	zlib_pre_call(strm);
	status = inflateInit2(&strm->z, windowBits);
	zlib_post_call(strm);
	if (status == Z_OK)
		return;
	die("inflateInit2: %s (%s)", zerr_to_string(status),
	    strm->z.msg ? strm->z.msg : "no message");
}

void git_inflate_end(git_zstream *strm)
{
	int status;

	zlib_pre_call(strm);
	status = inflateEnd(&strm->z);
	zlib_post_call(strm);
	if (status == Z_OK)
		return;
	error("inflateEnd: %s (%s)", zerr_to_string(status),
	      strm->z.msg ? strm->z.msg : "no message");
}

int git_inflate(git_zstream *strm, int flush)
{
	int status;

	for (;;) {
		zlib_pre_call(strm);
		/* Never say Z_FINISH unless we are feeding everything */
		status = inflate(&strm->z,
				 (strm->z.avail_in != strm->avail_in)
				 ? 0 : flush);
		if (status == Z_MEM_ERROR)
			die("inflate: out of memory");
		zlib_post_call(strm);

		/*
		 * Let zlib work another round, while we can still
		 * make progress.
		 */
		if ((strm->avail_out && !strm->z.avail_out) &&
		    (status == Z_OK || status == Z_BUF_ERROR))
			continue;
		break;
	}

	switch (status) {
	/* Z_BUF_ERROR: normal, needs more space in the output buffer */
	case Z_BUF_ERROR:
	case Z_OK:
	case Z_STREAM_END:
		return status;
	default:
		break;
	}
	error("inflate: %s (%s)", zerr_to_string(status),
	      strm->z.msg ? strm->z.msg : "no message");
	return status;
}

#if defined(NO_DEFLATE_BOUND) || ZLIB_VERNUM < 0x1200
#define deflateBound(c,s)  ((s) + (((s) + 7) >> 3) + (((s) + 63) >> 6) + 11)
#endif

size_t git_deflate_bound(git_zstream *strm, size_t size)
{
	return deflateBound(&strm->z, size);
}

void git_deflate_init(git_zstream *strm, int level)
{
	int status;

	memset(strm, 0, sizeof(*strm));
	zlib_pre_call(strm);
	status = deflateInit(&strm->z, level);
	zlib_post_call(strm);
	if (status == Z_OK)
		return;
	die("deflateInit: %s (%s)", zerr_to_string(status),
	    strm->z.msg ? strm->z.msg : "no message");
}

static void do_git_deflate_init(git_zstream *strm, int level, int windowBits)
{
	int status;

	memset(strm, 0, sizeof(*strm));
	zlib_pre_call(strm);
	status = deflateInit2(&strm->z, level,
				  Z_DEFLATED, windowBits,
				  8, Z_DEFAULT_STRATEGY);
	zlib_post_call(strm);
	if (status == Z_OK)
		return;
	die("deflateInit2: %s (%s)", zerr_to_string(status),
	    strm->z.msg ? strm->z.msg : "no message");
}

void git_deflate_init_gzip(git_zstream *strm, int level)
{
	/*
	 * Use default 15 bits, +16 is to generate gzip header/trailer
	 * instead of the zlib wrapper.
	 */
	do_git_deflate_init(strm, level, 15 + 16);
}

void git_deflate_init_raw(git_zstream *strm, int level)
{
	/*
	 * Use default 15 bits, negate the value to get raw compressed
	 * data without zlib header and trailer.
	 */
	do_git_deflate_init(strm, level, -15);
}

int git_deflate_abort(git_zstream *strm)
{
	int status;

	zlib_pre_call(strm);
	status = deflateEnd(&strm->z);
	zlib_post_call(strm);
	return status;
}

void git_deflate_end(git_zstream *strm)
{
	int status = git_deflate_abort(strm);

	if (status == Z_OK)
		return;
	error("deflateEnd: %s (%s)", zerr_to_string(status),
	      strm->z.msg ? strm->z.msg : "no message");
}

int git_deflate_end_gently(git_zstream *strm)
{
	int status;

	zlib_pre_call(strm);
	status = deflateEnd(&strm->z);
	zlib_post_call(strm);
	return status;
}

int git_deflate(git_zstream *strm, int flush)
{
	int status;

	for (;;) {
		zlib_pre_call(strm);

		/* Never say Z_FINISH unless we are feeding everything */
		status = deflate(&strm->z,
				 (strm->z.avail_in != strm->avail_in)
				 ? 0 : flush);
		if (status == Z_MEM_ERROR)
			die("deflate: out of memory");
		zlib_post_call(strm);

		/*
		 * Let zlib work another round, while we can still
		 * make progress.
		 */
		if ((strm->avail_out && !strm->z.avail_out) &&
		    (status == Z_OK || status == Z_BUF_ERROR))
			continue;
		break;
	}

	switch (status) {
	/* Z_BUF_ERROR: normal, needs more space in the output buffer */
	case Z_BUF_ERROR:
	case Z_OK:
	case Z_STREAM_END:
		return status;
	default:
		break;
	}
	error("deflate: %s (%s)", zerr_to_string(status),
	      strm->z.msg ? strm->z.msg : "no message");
	return status;
}
