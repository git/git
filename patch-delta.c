/*
 * patch-delta.c:
 * recreate a buffer from a source and the delta produced by diff-delta.c
 *
 * (C) 2005 Nicolas Pitre <nico@cam.org>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdlib.h>
#include <string.h>
#include "delta.h"

void *patch_delta(const void *src_buf, unsigned long src_size,
		  const void *delta_buf, unsigned long delta_size,
		  unsigned long *dst_size)
{
	const unsigned char *data, *top;
	unsigned char *dst_buf, *out, cmd;
	unsigned long size;

	if (delta_size < DELTA_SIZE_MIN)
		return NULL;

	data = delta_buf;
	top = delta_buf + delta_size;

	/* make sure the orig file size matches what we expect */
	size = get_delta_hdr_size(&data, top);
	if (size != src_size)
		return NULL;

	/* now the result size */
	size = get_delta_hdr_size(&data, top);
	dst_buf = malloc(size + 1);
	if (!dst_buf)
		return NULL;
	dst_buf[size] = 0;

	out = dst_buf;
	while (data < top) {
		cmd = *data++;
		if (cmd & 0x80) {
			unsigned long cp_off = 0, cp_size = 0;
			if (cmd & 0x01) cp_off = *data++;
			if (cmd & 0x02) cp_off |= (*data++ << 8);
			if (cmd & 0x04) cp_off |= (*data++ << 16);
			if (cmd & 0x08) cp_off |= (*data++ << 24);
			if (cmd & 0x10) cp_size = *data++;
			if (cmd & 0x20) cp_size |= (*data++ << 8);
			if (cmd & 0x40) cp_size |= (*data++ << 16);
			if (cp_size == 0) cp_size = 0x10000;
			if (cp_off + cp_size < cp_size ||
			    cp_off + cp_size > src_size ||
			    cp_size > size)
				goto bad;
			memcpy(out, src_buf + cp_off, cp_size);
			out += cp_size;
			size -= cp_size;
		} else if (cmd) {
			if (cmd > size)
				goto bad;
			memcpy(out, data, cmd);
			out += cmd;
			data += cmd;
			size -= cmd;
		} else {
			/*
			 * cmd == 0 is reserved for future encoding
			 * extensions. In the mean time we must fail when
			 * encountering them (might be data corruption).
			 */
			goto bad;
		}
	}

	/* sanity check */
	if (data != top || size != 0) {
		bad:
		free(dst_buf);
		return NULL;
	}

	*dst_size = out - dst_buf;
	return dst_buf;
}
