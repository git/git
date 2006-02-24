/*
 * Copyright (C) 2005 Junio C Hamano
 * The delta-parsing part is almost straight copy of patch-delta.c
 * which is (C) 2005 Nicolas Pitre <nico@cam.org>.
 */
#include "cache.h"
#include "delta.h"
#include "count-delta.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct span {
	struct span *next;
	unsigned long ofs;
	unsigned long end;
};

static void touch_range(struct span **span,
			unsigned long ofs, unsigned long end)
{
	struct span *e = *span;
	struct span *p = NULL;

	while (e && e->ofs <= ofs) {
		again:
		if (ofs < e->end) {
			while (e->end < end) {
				if (e->next && e->next->ofs <= end) {
					e->end = e->next->ofs;
					e = e->next;
				}
				else {
					e->end = end;
					return;
				}
			}
			return;
		}
		p = e;
		e = e->next;
	}
	if (e && e->ofs <= end) {
		e->ofs = ofs;
		goto again;
	}
	else {
		e = xmalloc(sizeof(*e));
		e->ofs = ofs;
		e->end = end;
		if (p) {
			e->next = p->next;
			p->next = e;
		}
		else {
			e->next = *span;
			*span = e;
		}
	}
}

static unsigned long count_range(struct span *s)
{
	struct span *t;
	unsigned long sz = 0;
	while (s) {
		t = s;
		sz += s->end - s->ofs;
		s = s->next;
		free(t);
	}
	return sz;
}

/*
 * NOTE.  We do not _interpret_ delta fully.  As an approximation, we
 * just count the number of bytes that are copied from the source, and
 * the number of literal data bytes that are inserted.
 *
 * Number of bytes that are _not_ copied from the source is deletion,
 * and number of inserted literal bytes are addition, so sum of them
 * is the extent of damage.
 */
int count_delta(void *delta_buf, unsigned long delta_size,
		unsigned long *src_copied, unsigned long *literal_added)
{
	unsigned long added_literal;
	const unsigned char *data, *top;
	unsigned char cmd;
	unsigned long src_size, dst_size, out;
	struct span *span = NULL;

	if (delta_size < DELTA_SIZE_MIN)
		return -1;

	data = delta_buf;
	top = delta_buf + delta_size;

	src_size = get_delta_hdr_size(&data);
	dst_size = get_delta_hdr_size(&data);

	added_literal = out = 0;
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

			touch_range(&span, cp_off, cp_off+cp_size);
			out += cp_size;
		} else {
			/* write literal into dst */
			added_literal += cmd;
			out += cmd;
			data += cmd;
		}
	}

	*src_copied = count_range(span);

	/* sanity check */
	if (data != top || out != dst_size)
		return -1;

	/* delete size is what was _not_ copied from source.
	 * edit size is that and literal additions.
	 */
	*literal_added = added_literal;
	return 0;
}
