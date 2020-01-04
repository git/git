#ifndef PKTLINE_H
#define PKTLINE_H

#include "git-compat-util.h"
#include "strbuf.h"
#include "sideband.h"

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
void packet_delim(int fd);
void packet_write_fmt(int fd, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
void packet_buf_flush(struct strbuf *buf);
void packet_buf_delim(struct strbuf *buf);
void set_packet_header(char *buf, int size);
void packet_write(int fd_out, const char *buf, size_t size);
void packet_buf_write(struct strbuf *buf, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
void packet_buf_write_len(struct strbuf *buf, const char *data, size_t len);
int packet_flush_gently(int fd);
int packet_write_fmt_gently(int fd, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
int write_packetized_from_fd(int fd_in, int fd_out);
int write_packetized_from_buf(const char *src_in, size_t len, int fd_out);

/*
 * Read a packetized line into the buffer, which must be at least size bytes
 * long. The return value specifies the number of bytes read into the buffer.
 *
 * If src_buffer and *src_buffer are not NULL, it should point to a buffer
 * containing the packet data to parse, of at least *src_len bytes.  After the
 * function returns, src_buf will be incremented and src_len decremented by the
 * number of bytes consumed.
 *
 * If src_buffer (or *src_buffer) is NULL, then data is read from the
 * descriptor "fd".
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
 *
 * If options contains PACKET_READ_CHOMP_NEWLINE, a trailing newline (if
 * present) is removed from the buffer before returning.
 *
 * If options contains PACKET_READ_DIE_ON_ERR_PACKET, it dies when it sees an
 * ERR packet.
 */
#define PACKET_READ_GENTLE_ON_EOF     (1u<<0)
#define PACKET_READ_CHOMP_NEWLINE     (1u<<1)
#define PACKET_READ_DIE_ON_ERR_PACKET (1u<<2)
int packet_read(int fd, char **src_buffer, size_t *src_len, char
		*buffer, unsigned size, int options);

/*
 * Read a packetized line into a buffer like the 'packet_read()' function but
 * returns an 'enum packet_read_status' which indicates the status of the read.
 * The number of bytes read will be assigned to *pktlen if the status of the
 * read was 'PACKET_READ_NORMAL'.
 */
enum packet_read_status {
	PACKET_READ_EOF,
	PACKET_READ_NORMAL,
	PACKET_READ_FLUSH,
	PACKET_READ_DELIM,
};
enum packet_read_status packet_read_with_status(int fd, char **src_buffer,
						size_t *src_len, char *buffer,
						unsigned size, int *pktlen,
						int options);

/*
 * Convenience wrapper for packet_read that is not gentle, and sets the
 * CHOMP_NEWLINE option. The return value is NULL for a flush packet,
 * and otherwise points to a static buffer (that may be overwritten by
 * subsequent calls). If the size parameter is not NULL, the length of the
 * packet is written to it.
 */
char *packet_read_line(int fd, int *size);

/*
 * Convenience wrapper for packet_read that sets the PACKET_READ_GENTLE_ON_EOF
 * and CHOMP_NEWLINE options. The return value specifies the number of bytes
 * read into the buffer or -1 on truncated input. If the *dst_line parameter
 * is not NULL it will return NULL for a flush packet or when the number of
 * bytes copied is zero and otherwise points to a static buffer (that may be
 * overwritten by subsequent calls). If the size parameter is not NULL, the
 * length of the packet is written to it.
 */
int packet_read_line_gently(int fd, int *size, char **dst_line);

/*
 * Same as packet_read_line, but read from a buf rather than a descriptor;
 * see packet_read for details on how src_* is used.
 */
char *packet_read_line_buf(char **src_buf, size_t *src_len, int *size);

/*
 * Reads a stream of variable sized packets until a flush packet is detected.
 */
ssize_t read_packetized_to_strbuf(int fd_in, struct strbuf *sb_out);

/*
 * Receive multiplexed output stream over git native protocol.
 * in_stream is the input stream from the remote, which carries data
 * in pkt_line format with band designator.  Demultiplex it into out
 * and err and return error appropriately.  Band #1 carries the
 * primary payload.  Things coming over band #2 is not necessarily
 * error; they are usually informative message on the standard error
 * stream, aka "verbose").  A message over band #3 is a signal that
 * the remote died unexpectedly.  A flush() concludes the stream.
 *
 * Returns SIDEBAND_FLUSH upon a normal conclusion, and SIDEBAND_PROTOCOL_ERROR
 * or SIDEBAND_REMOTE_ERROR if an error occurred.
 */
int recv_sideband(const char *me, int in_stream, int out);

struct packet_reader {
	/* source file descriptor */
	int fd;

	/* source buffer and its size */
	char *src_buffer;
	size_t src_len;

	/* buffer that pkt-lines are read into and its size */
	char *buffer;
	unsigned buffer_size;

	/* options to be used during reads */
	int options;

	/* status of the last read */
	enum packet_read_status status;

	/* length of data read during the last read */
	int pktlen;

	/* the last line read */
	const char *line;

	/* indicates if a line has been peeked */
	int line_peeked;

	unsigned use_sideband : 1;
	const char *me;
};

/*
 * Initialize a 'struct packet_reader' object which is an
 * abstraction around the 'packet_read_with_status()' function.
 */
void packet_reader_init(struct packet_reader *reader, int fd,
			char *src_buffer, size_t src_len,
			int options);

/*
 * Perform a packet read and return the status of the read.
 * The values of 'pktlen' and 'line' are updated based on the status of the
 * read as follows:
 *
 * PACKET_READ_ERROR: 'pktlen' is set to '-1' and 'line' is set to NULL
 * PACKET_READ_NORMAL: 'pktlen' is set to the number of bytes read
 *		       'line' is set to point at the read line
 * PACKET_READ_FLUSH: 'pktlen' is set to '0' and 'line' is set to NULL
 */
enum packet_read_status packet_reader_read(struct packet_reader *reader);

/*
 * Peek the next packet line without consuming it and return the status.
 * The next call to 'packet_reader_read()' will perform a read of the same line
 * that was peeked, consuming the line.
 *
 * Peeking multiple times without calling 'packet_reader_read()' will return
 * the same result.
 */
enum packet_read_status packet_reader_peek(struct packet_reader *reader);

#define DEFAULT_PACKET_MAX 1000
#define LARGE_PACKET_MAX 65520
#define LARGE_PACKET_DATA_MAX (LARGE_PACKET_MAX - 4)
extern char packet_buffer[LARGE_PACKET_MAX];

struct packet_writer {
	int dest_fd;
	unsigned use_sideband : 1;
};

void packet_writer_init(struct packet_writer *writer, int dest_fd);

/* These functions die upon failure. */
__attribute__((format (printf, 2, 3)))
void packet_writer_write(struct packet_writer *writer, const char *fmt, ...);
__attribute__((format (printf, 2, 3)))
void packet_writer_error(struct packet_writer *writer, const char *fmt, ...);
void packet_writer_delim(struct packet_writer *writer);
void packet_writer_flush(struct packet_writer *writer);

#endif
