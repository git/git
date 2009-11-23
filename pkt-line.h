#ifndef PKTLINE_H
#define PKTLINE_H

#include "git-compat-util.h"
#include "strbuf.h"

/*
 * Silly packetized line writing interface
 */
void packet_flush(int fd);
void packet_write(int fd, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
void packet_buf_flush(struct strbuf *buf);
void packet_buf_write(struct strbuf *buf, const char *fmt, ...) __attribute__((format (printf, 2, 3)));

int packet_read_line(int fd, char *buffer, unsigned size);
int packet_get_line(struct strbuf *out, char **src_buf, size_t *src_len);
ssize_t safe_write(int, const void *, ssize_t);

#endif
