#include "git-compat-util.h"

#if ZLIB_VERNUM < 0x1290
/* taken from zlib's uncompr.c

   commit cacf7f1d4e3d44d871b605da3b647f07d718623f
   Author: Mark Adler <madler@alumni.caltech.edu>
   Date:   Sun Jan 15 09:18:46 2017 -0800

       zlib 1.2.11

*/

/*
 * Copyright (C) 1995-2003, 2010, 2014, 2016 Jean-loup Gailly, Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/* clang-format off */

/* ===========================================================================
     Decompresses the source buffer into the destination buffer.  *sourceLen is
   the byte length of the source buffer. Upon entry, *destLen is the total size
   of the destination buffer, which must be large enough to hold the entire
   uncompressed data. (The size of the uncompressed data must have been saved
   previously by the compressor and transmitted to the decompressor by some
   mechanism outside the scope of this compression library.) Upon exit,
   *destLen is the size of the decompressed data and *sourceLen is the number
   of source bytes consumed. Upon return, source + *sourceLen points to the
   first unused input byte.

     uncompress returns Z_OK if success, Z_MEM_ERROR if there was not enough
   memory, Z_BUF_ERROR if there was not enough room in the output buffer, or
   Z_DATA_ERROR if the input data was corrupted, including if the input data is
   an incomplete zlib stream.
*/
int ZEXPORT uncompress2 (
    Bytef *dest,
    uLongf *destLen,
    const Bytef *source,
    uLong *sourceLen) {
    z_stream stream;
    int err;
    const uInt max = (uInt)-1;
    uLong len, left;
    Byte buf[1];    /* for detection of incomplete stream when *destLen == 0 */

    len = *sourceLen;
    if (*destLen) {
	left = *destLen;
	*destLen = 0;
    }
    else {
	left = 1;
	dest = buf;
    }

    stream.next_in = (z_const Bytef *)source;
    stream.avail_in = 0;
    stream.zalloc = (alloc_func)0;
    stream.zfree = (free_func)0;
    stream.opaque = (voidpf)0;

    err = inflateInit(&stream);
    if (err != Z_OK) return err;

    stream.next_out = dest;
    stream.avail_out = 0;

    do {
	if (stream.avail_out == 0) {
	    stream.avail_out = left > (uLong)max ? max : (uInt)left;
	    left -= stream.avail_out;
	}
	if (stream.avail_in == 0) {
	    stream.avail_in = len > (uLong)max ? max : (uInt)len;
	    len -= stream.avail_in;
	}
	err = inflate(&stream, Z_NO_FLUSH);
    } while (err == Z_OK);

    *sourceLen -= len + stream.avail_in;
    if (dest != buf)
	*destLen = stream.total_out;
    else if (stream.total_out && err == Z_BUF_ERROR)
	left = 1;

    inflateEnd(&stream);
    return err == Z_STREAM_END ? Z_OK :
	   err == Z_NEED_DICT ? Z_DATA_ERROR  :
	   err == Z_BUF_ERROR && left + stream.avail_out ? Z_DATA_ERROR :
	   err;
}
#else
static void *dummy_variable = &dummy_variable;
#endif
