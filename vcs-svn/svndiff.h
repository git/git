#ifndef SVNDIFF_H
#define SVNDIFF_H

struct line_buffer;
struct sliding_view;

extern int svndiff0_apply(struct line_buffer *delta, off_t delta_len,
		struct sliding_view *preimage, FILE *postimage);

#endif
