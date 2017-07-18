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
 * instructions ::= instruction*;
 * instruction ::= view_selector int int
 *   | copyfrom_data int
 *   | packed_view_selector int
 *   | packed_copyfrom_data
 *   ;
 * view_selector ::= copyfrom_source
 *   | copyfrom_target
 *   ;
 * copyfrom_source ::= # binary 00 000000;
 * copyfrom_target ::= # binary 01 000000;
 * copyfrom_data ::= # binary 10 000000;
 * packed_view_selector ::= # view_selector OR-ed with 6 bit value;
 * packed_copyfrom_data ::= # copyfrom_data OR-ed with 6 bit value;
 * int ::= highdigit* lowdigit;
 * highdigit ::= # binary 1000 0000 OR-ed with 7 bit value;
 * lowdigit ::= # 7 bit value;
 */

#define INSN_MASK	0xc0
#define INSN_COPYFROM_SOURCE	0x00
#define INSN_COPYFROM_TARGET	0x40
#define INSN_COPYFROM_DATA	0x80
#define OPERAND_MASK	0x3f

#define VLI_CONTINUE	0x80
#define VLI_DIGIT_MASK	0x7f
#define VLI_BITS_PER_DIGIT 7

struct window {
	struct sliding_view *in;
	struct strbuf out;
	struct strbuf instructions;
	struct strbuf data;
};

#define WINDOW_INIT(w)	{ (w), STRBUF_INIT, STRBUF_INIT, STRBUF_INIT }

static void window_release(struct window *ctx)
{
	strbuf_release(&ctx->out);
	strbuf_release(&ctx->instructions);
	strbuf_release(&ctx->data);
}

static int write_strbuf(struct strbuf *sb, FILE *out)
{
	if (fwrite(sb->buf, 1, sb->len, out) == sb->len)	/* Success. */
		return 0;
	return error("cannot write delta postimage: %s", strerror(errno));
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
	assert(*delta_len >= 0);
	strbuf_reset(buf);
	if (len > (uintmax_t) *delta_len ||
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

static int parse_int(const char **buf, size_t *result, const char *end)
{
	size_t rv = 0;
	const char *pos;
	for (pos = *buf; pos != end; pos++) {
		unsigned char ch = *pos;

		rv <<= VLI_BITS_PER_DIGIT;
		rv += (ch & VLI_DIGIT_MASK);
		if (ch & VLI_CONTINUE)
			continue;

		*result = rv;
		*buf = pos + 1;
		return 0;
	}
	return error("invalid delta: unexpected end of instructions section");
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

static int copyfrom_source(struct window *ctx, const char **instructions,
			   size_t nbytes, const char *insns_end)
{
	size_t offset;
	if (parse_int(instructions, &offset, insns_end))
		return -1;
	if (unsigned_add_overflows(offset, nbytes) ||
	    offset + nbytes > ctx->in->width)
		return error("invalid delta: copies source data outside view");
	strbuf_add(&ctx->out, ctx->in->buf.buf + offset, nbytes);
	return 0;
}

static int copyfrom_target(struct window *ctx, const char **instructions,
			   size_t nbytes, const char *instructions_end)
{
	size_t offset;
	if (parse_int(instructions, &offset, instructions_end))
		return -1;
	if (offset >= ctx->out.len)
		return error("invalid delta: copies from the future");
	for (; nbytes > 0; nbytes--)
		strbuf_addch(&ctx->out, ctx->out.buf[offset++]);
	return 0;
}

static int copyfrom_data(struct window *ctx, size_t *data_pos, size_t nbytes)
{
	const size_t pos = *data_pos;
	if (unsigned_add_overflows(pos, nbytes) ||
	    pos + nbytes > ctx->data.len)
		return error("invalid delta: copies unavailable inline data");
	strbuf_add(&ctx->out, ctx->data.buf + pos, nbytes);
	*data_pos += nbytes;
	return 0;
}

static int parse_first_operand(const char **buf, size_t *out, const char *end)
{
	size_t result = (unsigned char) *(*buf)++ & OPERAND_MASK;
	if (result) {	/* immediate operand */
		*out = result;
		return 0;
	}
	return parse_int(buf, out, end);
}

static int execute_one_instruction(struct window *ctx,
				const char **instructions, size_t *data_pos)
{
	unsigned int instruction;
	const char *insns_end = ctx->instructions.buf + ctx->instructions.len;
	size_t nbytes;
	assert(ctx);
	assert(instructions && *instructions);
	assert(data_pos);

	instruction = (unsigned char) **instructions;
	if (parse_first_operand(instructions, &nbytes, insns_end))
		return -1;
	switch (instruction & INSN_MASK) {
	case INSN_COPYFROM_SOURCE:
		return copyfrom_source(ctx, instructions, nbytes, insns_end);
	case INSN_COPYFROM_TARGET:
		return copyfrom_target(ctx, instructions, nbytes, insns_end);
	case INSN_COPYFROM_DATA:
		return copyfrom_data(ctx, data_pos, nbytes);
	default:
		return error("invalid delta: unrecognized instruction");
	}
}

static int apply_window_in_core(struct window *ctx)
{
	const char *instructions;
	size_t data_pos = 0;

	/*
	 * Fill ctx->out.buf using data from the source, target,
	 * and inline data views.
	 */
	for (instructions = ctx->instructions.buf;
	     instructions != ctx->instructions.buf + ctx->instructions.len;
	     )
		if (execute_one_instruction(ctx, &instructions, &data_pos))
			return -1;
	if (data_pos != ctx->data.len)
		return error("invalid delta: does not copy all inline data");
	return 0;
}

static int apply_one_window(struct line_buffer *delta, off_t *delta_len,
			    struct sliding_view *preimage, FILE *out)
{
	int rv = -1;
	struct window ctx = WINDOW_INIT(preimage);
	size_t out_len;
	size_t instructions_len;
	size_t data_len;
	assert(delta_len);

	/* "source view" offset and length already handled; */
	if (read_length(delta, &out_len, delta_len) ||
	    read_length(delta, &instructions_len, delta_len) ||
	    read_length(delta, &data_len, delta_len) ||
	    read_chunk(delta, delta_len, &ctx.instructions, instructions_len) ||
	    read_chunk(delta, delta_len, &ctx.data, data_len))
		goto error_out;
	strbuf_grow(&ctx.out, out_len);
	if (apply_window_in_core(&ctx))
		goto error_out;
	if (ctx.out.len != out_len) {
		rv = error("invalid delta: incorrect postimage length");
		goto error_out;
	}
	if (write_strbuf(&ctx.out, out))
		goto error_out;
	rv = 0;
error_out:
	window_release(&ctx);
	return rv;
}

int svndiff0_apply(struct line_buffer *delta, off_t delta_len,
			struct sliding_view *preimage, FILE *postimage)
{
	assert(delta && preimage && postimage && delta_len >= 0);

	if (read_magic(delta, &delta_len))
		return -1;
	while (delta_len) {	/* For each window: */
		off_t pre_off = -1;
		size_t pre_len;

		if (read_offset(delta, &pre_off, &delta_len) ||
		    read_length(delta, &pre_len, &delta_len) ||
		    move_window(preimage, pre_off, pre_len) ||
		    apply_one_window(delta, &delta_len, preimage, postimage))
			return -1;
	}
	return 0;
}
