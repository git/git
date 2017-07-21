/*
 * Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "git-compat-util.h"
#include "sliding_window.h"
#include "line_buffer.h"
#include "strbuf.h"

static int input_error(struct line_buffer *file)
{
	if (!buffer_ferror(file))
		return error("delta preimage ends early");
	return error_errno("cannot read delta preimage");
}

static int skip_or_whine(struct line_buffer *file, off_t gap)
{
	if (buffer_skip_bytes(file, gap) != gap)
		return input_error(file);
	return 0;
}

static int read_to_fill_or_whine(struct line_buffer *file,
				struct strbuf *buf, size_t width)
{
	buffer_read_binary(file, buf, width - buf->len);
	if (buf->len != width)
		return input_error(file);
	return 0;
}

static int check_offset_overflow(off_t offset, uintmax_t len)
{
	if (len > maximum_signed_value_of_type(off_t))
		return error("unrepresentable length in delta: "
				"%"PRIuMAX" > OFF_MAX", len);
	if (signed_add_overflows(offset, (off_t) len))
		return error("unrepresentable offset in delta: "
				"%"PRIuMAX" + %"PRIuMAX" > OFF_MAX",
				(uintmax_t) offset, len);
	return 0;
}

int move_window(struct sliding_view *view, off_t off, size_t width)
{
	off_t file_offset;
	assert(view);
	assert(view->width <= view->buf.len);
	assert(!check_offset_overflow(view->off, view->buf.len));

	if (check_offset_overflow(off, width))
		return -1;
	if (off < view->off || off + width < view->off + view->width)
		return error("invalid delta: window slides left");
	if (view->max_off >= 0 && view->max_off < off + (off_t) width)
		return error("delta preimage ends early");

	file_offset = view->off + view->buf.len;
	if (off < file_offset) {
		/* Move the overlapping region into place. */
		strbuf_remove(&view->buf, 0, off - view->off);
	} else {
		/* Seek ahead to skip the gap. */
		if (skip_or_whine(view->file, off - file_offset))
			return -1;
		strbuf_setlen(&view->buf, 0);
	}

	if (view->buf.len > width)
		; /* Already read. */
	else if (read_to_fill_or_whine(view->file, &view->buf, width))
		return -1;

	view->off = off;
	view->width = width;
	return 0;
}
