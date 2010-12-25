/*
 * Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#include "git-compat-util.h"
#include "line_buffer.h"
#include "svndiff.h"

/*
 * svndiff0 applier
 *
 * See http://svn.apache.org/repos/asf/subversion/trunk/notes/svndiff.
 *
 * svndiff0 ::= 'SVN\0' window*
 */

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
	    buffer_read_binary(in, &sb, sizeof(magic)) != sizeof(magic))
		return error_short_read(in);

	if (memcmp(sb.buf, magic, sizeof(magic)))
		return error("invalid delta: unrecognized file type");

	*len -= sizeof(magic);
	strbuf_release(&sb);
	return 0;
}

int svndiff0_apply(struct line_buffer *delta, off_t delta_len,
			struct sliding_view *preimage, FILE *postimage)
{
	assert(delta && preimage && postimage);

	if (read_magic(delta, &delta_len))
		return -1;
	if (delta_len)
		return error("What do you think I am?  A delta applier?");
	return 0;
}
