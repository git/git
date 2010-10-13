/*
 * Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "git-compat-util.h"
#include "sliding_window.h"
#include "line_buffer.h"
#include "svndiff.h"

/*
 * svndiff0 applier
 *
 * See http://svn.apache.org/repos/asf/subversion/trunk/notes/svndiff.
 *
 * svndiff0 ::= 'SVN\0' window*
 * window ::= int int int int int instructions inline_data;
 * int ::= highdigit* lowdigit;
 * highdigit ::= # binary 1000 0000 OR-ed with 7 bit value;
 * lowdigit ::= # 7 bit value;
 */

#define VLI_CONTINUE	0x80
#define VLI_DIGIT_MASK	0x7f
#define VLI_BITS_PER_DIGIT 7

struct window {
	struct strbuf data;
};

#define WINDOW_INIT	{ STRBUF_INIT }

static void window_release(struct window *ctx)
{
	strbuf_release(&ctx->data);
}

static int error_short_read(struct line_buffer *input)
{
	if (buffer_ferror(input))
		return error("error reading delta: %s", strerror(errno));
	return error("invalid delta: unexpected end of file");
}

static int read_chunk(struct line_buffer *delta, off_t *delta_len,
		      struct strbuf *buf, size_t len)
{
	strbuf_reset(buf);
	if (len > *delta_len ||
	    buffer_read_binary(delta, buf, len) != len)
		return error_short_read(delta);
	*delta_len -= buf->len;
	return 0;
}

static int read_magic(struct line_buffer *in, off_t *len)
{
	static const char magic[] = {'S', 'V', 'N', '\0'};
	struct strbuf sb = STRBUF_INIT;

	if (read_chunk(in, len, &sb, sizeof(magic))) {
		strbuf_release(&sb);
		return -1;
	}
	if (memcmp(sb.buf, magic, sizeof(magic))) {
		strbuf_release(&sb);
		return error("invalid delta: unrecognized file type");
	}
	strbuf_release(&sb);
	return 0;
}

static int read_int(struct line_buffer *in, uintmax_t *result, off_t *len)
{
	uintmax_t rv = 0;
	off_t sz;
	for (sz = *len; sz; sz--) {
		const int ch = buffer_read_char(in);
		if (ch == EOF)
			break;

		rv <<= VLI_BITS_PER_DIGIT;
		rv += (ch & VLI_DIGIT_MASK);
		if (ch & VLI_CONTINUE)
			continue;

		*result = rv;
		*len = sz - 1;
		return 0;
	}
	return error_short_read(in);
}

static int read_offset(struct line_buffer *in, off_t *result, off_t *len)
{
	uintmax_t val;
	if (read_int(in, &val, len))
		return -1;
	if (val > maximum_signed_value_of_type(off_t))
		return error("unrepresentable offset in delta: %"PRIuMAX"", val);
	*result = val;
	return 0;
}

static int read_length(struct line_buffer *in, size_t *result, off_t *len)
{
	uintmax_t val;
	if (read_int(in, &val, len))
		return -1;
	if (val > SIZE_MAX)
		return error("unrepresentable length in delta: %"PRIuMAX"", val);
	*result = val;
	return 0;
}

static int apply_one_window(struct line_buffer *delta, off_t *delta_len)
{
	struct window ctx = WINDOW_INIT;
	size_t out_len;
	size_t instructions_len;
	size_t data_len;
	assert(delta_len);

	/* "source view" offset and length already handled; */
	if (read_length(delta, &out_len, delta_len) ||
	    read_length(delta, &instructions_len, delta_len) ||
	    read_length(delta, &data_len, delta_len))
		goto error_out;
	if (instructions_len) {
		error("What do you think I am?  A delta applier?");
		goto error_out;
	}
	if (read_chunk(delta, delta_len, &ctx.data, data_len))
		goto error_out;
	window_release(&ctx);
	return 0;
error_out:
	window_release(&ctx);
	return -1;
}

int svndiff0_apply(struct line_buffer *delta, off_t delta_len,
			struct sliding_view *preimage, FILE *postimage)
{
	assert(delta && preimage && postimage);

	if (read_magic(delta, &delta_len))
		return -1;
	while (delta_len) {	/* For each window: */
		off_t pre_off;
		size_t pre_len;

		if (read_offset(delta, &pre_off, &delta_len) ||
		    read_length(delta, &pre_len, &delta_len) ||
		    move_window(preimage, pre_off, pre_len) ||
		    apply_one_window(delta, &delta_len))
			return -1;
	}
	return 0;
}
