#ifndef LINE_BUFFER_H_
#define LINE_BUFFER_H_

#include "strbuf.h"

#define LINE_BUFFER_LEN 10000

struct line_buffer {
	char line_buffer[LINE_BUFFER_LEN];
	struct strbuf blob_buffer;
	FILE *infile;
};
#define LINE_BUFFER_INIT {"", STRBUF_INIT, NULL}

int buffer_init(struct line_buffer *buf, const char *filename);
int buffer_deinit(struct line_buffer *buf);
char *buffer_read_line(struct line_buffer *buf);
char *buffer_read_string(struct line_buffer *buf, uint32_t len);
void buffer_read_binary(struct line_buffer *buf, struct strbuf *sb, uint32_t len);
void buffer_copy_bytes(struct line_buffer *buf, uint32_t len);
void buffer_skip_bytes(struct line_buffer *buf, uint32_t len);
void buffer_reset(struct line_buffer *buf);

#endif
