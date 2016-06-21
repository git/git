/*
 * Copyright (C) 2005 Junio C Hamano
 * The delta-parsing part is almost straight copy of patch-delta.c
 * which is (C) 2005 Nicolas Pitre <nico@cam.org>.
 */
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "delta.h"
#include "count-delta.h"

/*
 * NOTE.  We do not _interpret_ delta fully.  As an approximation, we
 * just count the number of bytes that are copied from the source, and
 * the number of literal data bytes that are inserted.
 *
 * Number of bytes that are _not_ copied from the source is deletion,
 * and number of inserted literal bytes are addition, so sum of them
 * is the extent of damage.  xdelta can express an edit that copies
 * data inside of the destination which originally came from the
 * source.  We do not count that in the following routine, so we are
 * undercounting the source material that remains in the final output
 * that way.
 */
int count_delta(void *delta_buf, unsigned long delta_size,
		unsigned long *src_copied, unsigned long *literal_added)
{
	unsigned long copied_from_source, added_literal;
	const unsigned char *data, *top;
	unsigned char cmd;
	unsigned long src_size, dst_size, out;

	if (delta_size < DELTA_SIZE_MIN)
		return -1;

	data = delta_buf;
	top = delta_buf + delta_size;

	src_size = get_delta_hdr_size(&data);
	dst_size = get_delta_hdr_size(&data);

	added_literal = copied_from_source = out = 0;
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
			if (cp_size == 0) cp_size = 0x10000;

			if (cmd & 0x40)
				/* copy from dst */
				;
			else
				copied_from_source += cp_size;
			out += cp_size;
		} else {
			/* write literal into dst */
			added_literal += cmd;
			out += cmd;
			data += cmd;
		}
	}

	/* sanity check */
	if (data != top || out != dst_size)
		return -1;

	/* delete size is what was _not_ copied from source.
	 * edit size is that and literal additions.
	 */
	*src_copied = copied_from_source;
	*literal_added = added_literal;
	return 0;
}
