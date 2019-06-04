#ifndef LINE_BUFFER_H
#define LINE_BUFFER_H

#include "strbuf.h"

#define LINE_BUFFER_LEN 10000

struct line_buffer {
	char line_buffer[LINE_BUFFER_LEN];
	FILE *infile;
};
#define LINE_BUFFER_INIT { "", NULL }

int buffer_init(struct line_buffer *buf, const char *filename);
int buffer_fdinit(struct line_buffer *buf, int fd);
int buffer_deinit(struct line_buffer *buf);

int buffer_tmpfile_init(struct line_buffer *buf);
FILE *buffer_tmpfile_rewind(struct line_buffer *buf);	/* prepare to write. */
long buffer_tmpfile_prepare_to_read(struct line_buffer *buf);

int buffer_ferror(struct line_buffer *buf);
char *buffer_read_line(struct line_buffer *buf);
int buffer_read_char(struct line_buffer *buf);
size_t buffer_read_binary(struct line_buffer *buf, struct strbuf *sb, size_t len);
/* Returns number of bytes read (not necessarily written). */
off_t buffer_copy_bytes(struct line_buffer *buf, off_t len);
off_t buffer_skip_bytes(struct line_buffer *buf, off_t len);

#endif
