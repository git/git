#ifndef PKTLINE_H
#define PKTLINE_H

#include "git-compat-util.h"
#include "strbuf.h"

/*
 * Write a packetized stream, where each line is preceded by
 * its length (including the header) as a 4-byte hex number.
 * A length of 'zero' means end of stream (and a length of 1-3
 * would be an error).
 *
 * This is all pretty stupid, but we use this packetized line
 * format to make a streaming format possible without ever
 * over-running the read buffers. That way we'll never read
 * into what might be the pack data (which should go to another
 * process entirely).
 *
 * The writing side could use stdio, but since the reading
 * side can't, we stay with pure read/write interfaces.
 */
void packet_flush(int fd);
void packet_write(int fd, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
void packet_buf_flush(struct strbuf *buf);
void packet_buf_write(struct strbuf *buf, const char *fmt, ...) __attribute__((format (printf, 2, 3)));

/*
 * Read a packetized line from the descriptor into the buffer, which must be at
 * least size bytes long. The return value specifies the number of bytes read
 * into the buffer.
 *
 * If options does not contain PACKET_READ_GENTLE_ON_EOF, we will die under any
 * of the following conditions:
 *
 *   1. Read error from descriptor.
 *
 *   2. Protocol error from the remote (e.g., bogus length characters).
 *
 *   3. Receiving a packet larger than "size" bytes.
 *
 *   4. Truncated output from the remote (e.g., we expected a packet but got
 *      EOF, or we got a partial packet followed by EOF).
 *
 * If options does contain PACKET_READ_GENTLE_ON_EOF, we will not die on
 * condition 4 (truncated input), but instead return -1. However, we will still
 * die for the other 3 conditions.
 */
#define PACKET_READ_GENTLE_ON_EOF (1u<<0)
int packet_read(int fd, char *buffer, unsigned size, int options);

/* Historical convenience wrapper for packet_read that sets no options */
int packet_read_line(int fd, char *buffer, unsigned size);

int packet_get_line(struct strbuf *out, char **src_buf, size_t *src_len);

#endif
