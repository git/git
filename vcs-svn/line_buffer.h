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

int buffer_init(const char *filename);
int buffer_deinit(void);
char *buffer_read_line(void);
char *buffer_read_string(uint32_t len);
void buffer_copy_bytes(uint32_t len);
void buffer_skip_bytes(uint32_t len);
void buffer_reset(void);

#endif
