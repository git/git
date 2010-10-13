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

static int error_short_read(struct line_buffer *input)
{
	if (buffer_ferror(input))
		return error("error reading delta: %s", strerror(errno));
	return error("invalid delta: unexpected end of file");
}

static int read_magic(struct line_buffer *in, off_t *len)
{
	static const char magic[] = {'S', 'V', 'N', '\0'};
	struct strbuf sb = STRBUF_INIT;

	if (*len < sizeof(magic) ||
	    buffer_read_binary(in, &sb, sizeof(magic)) != sizeof(magic)) {
		error_short_read(in);
		strbuf_release(&sb);
		return -1;
	}

	if (memcmp(sb.buf, magic, sizeof(magic))) {
		strbuf_release(&sb);
		return error("invalid delta: unrecognized file type");
	}

	*len -= sizeof(magic);
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
	size_t out_len;
	size_t instructions_len;
	size_t data_len;
	assert(delta_len);

	/* "source view" offset and length already handled; */
	if (read_length(delta, &out_len, delta_len) ||
	    read_length(delta, &instructions_len, delta_len) ||
	    read_length(delta, &data_len, delta_len))
		return -1;
	if (instructions_len)
		return error("What do you think I am?  A delta applier?");
	if (data_len)
		return error("No support for inline data yet");
	return 0;
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
