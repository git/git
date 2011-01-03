#ifndef SLIDING_WINDOW_H_
#define SLIDING_WINDOW_H_

#include "strbuf.h"

struct sliding_view {
	struct line_buffer *file;
	off_t off;
	size_t width;
	struct strbuf buf;
};

#define SLIDING_VIEW_INIT(input)	{ (input), 0, 0, STRBUF_INIT }

extern int move_window(struct sliding_view *view, off_t off, size_t width);

#endif
