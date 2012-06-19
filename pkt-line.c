#include "cache.h"
#include "pkt-line.h"

static const char *packet_trace_prefix = "git";
static const char trace_key[] = "GIT_TRACE_PACKET";

void packet_trace_identity(const char *prog)
{
	packet_trace_prefix = xstrdup(prog);
}

static void packet_trace(const char *buf, unsigned int len, int write)
{
	int i;
	struct strbuf out;

	if (!trace_want(trace_key))
		return;

	/* +32 is just a guess for header + quoting */
	strbuf_init(&out, len+32);

	strbuf_addf(&out, "packet: %12s%c ",
		    packet_trace_prefix, write ? '>' : '<');

	if ((len >= 4 && !prefixcmp(buf, "PACK")) ||
	    (len >= 5 && !prefixcmp(buf+1, "PACK"))) {
		strbuf_addstr(&out, "PACK ...");
		unsetenv(trace_key);
	}
	else {
		/* XXX we should really handle printable utf8 */
		for (i = 0; i < len; i++) {
			/* suppress newlines */
			if (buf[i] == '\n')
				continue;
			if (buf[i] >= 0x20 && buf[i] <= 0x7e)
				strbuf_addch(&out, buf[i]);
			else
				strbuf_addf(&out, "\\%o", buf[i]);
		}
	}

	strbuf_addch(&out, '\n');
	trace_strbuf(trace_key, &out);
	strbuf_release(&out);
}

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
ssize_t safe_write(int fd, const void *buf, ssize_t n)
{
	ssize_t nn = n;
	while (n) {
		int ret = xwrite(fd, buf, n);
		if (ret > 0) {
			buf = (char *) buf + ret;
			n -= ret;
			continue;
		}
		if (!ret)
			die("write error (disk full?)");
		die_errno("write error");
	}
	return nn;
}

/*
 * If we buffered things up above (we don't, but we should),
 * we'd flush it here
 */
void packet_flush(int fd)
{
	packet_trace("0000", 4, 1);
	safe_write(fd, "0000", 4);
}

void packet_buf_flush(struct strbuf *buf)
{
	packet_trace("0000", 4, 1);
	strbuf_add(buf, "0000", 4);
}

#define hex(a) (hexchar[(a) & 15])
static char buffer[1000];
static unsigned format_packet(const char *fmt, va_list args)
{
	static char hexchar[] = "0123456789abcdef";
	unsigned n;

	n = vsnprintf(buffer + 4, sizeof(buffer) - 4, fmt, args);
	if (n >= sizeof(buffer)-4)
		die("protocol error: impossibly long line");
	n += 4;
	buffer[0] = hex(n >> 12);
	buffer[1] = hex(n >> 8);
	buffer[2] = hex(n >> 4);
	buffer[3] = hex(n);
	packet_trace(buffer+4, n-4, 1);
	return n;
}

void packet_write(int fd, const char *fmt, ...)
{
	va_list args;
	unsigned n;

	va_start(args, fmt);
	n = format_packet(fmt, args);
	va_end(args);
	safe_write(fd, buffer, n);
}

void packet_buf_write(struct strbuf *buf, const char *fmt, ...)
{
	va_list args;
	unsigned n;

	va_start(args, fmt);
	n = format_packet(fmt, args);
	va_end(args);
	strbuf_add(buf, buffer, n);
}

static int safe_read(int fd, void *buffer, unsigned size, int return_line_fail)
{
	ssize_t ret = read_in_full(fd, buffer, size);
	if (ret < 0)
		die_errno("read error");
	else if (ret < size) {
		if (return_line_fail)
			return -1;

		die("The remote end hung up unexpectedly");
	}

	return ret;
}

static int packet_length(const char *linelen)
{
	int n;
	int len = 0;

	for (n = 0; n < 4; n++) {
		unsigned char c = linelen[n];
		len <<= 4;
		if (c >= '0' && c <= '9') {
			len += c - '0';
			continue;
		}
		if (c >= 'a' && c <= 'f') {
			len += c - 'a' + 10;
			continue;
		}
		if (c >= 'A' && c <= 'F') {
			len += c - 'A' + 10;
			continue;
		}
		return -1;
	}
	return len;
}

static int packet_read_internal(int fd, char *buffer, unsigned size, int return_line_fail)
{
	int len, ret;
	char linelen[4];

	ret = safe_read(fd, linelen, 4, return_line_fail);
	if (return_line_fail && ret < 0)
		return ret;
	len = packet_length(linelen);
	if (len < 0)
		die("protocol error: bad line length character: %.4s", linelen);
	if (!len) {
		packet_trace("0000", 4, 0);
		return 0;
	}
	len -= 4;
	if (len >= size)
		die("protocol error: bad line length %d", len);
	ret = safe_read(fd, buffer, len, return_line_fail);
	if (return_line_fail && ret < 0)
		return ret;
	buffer[len] = 0;
	packet_trace(buffer, len, 0);
	return len;
}

int packet_read(int fd, char *buffer, unsigned size)
{
	return packet_read_internal(fd, buffer, size, 1);
}

int packet_read_line(int fd, char *buffer, unsigned size)
{
	return packet_read_internal(fd, buffer, size, 0);
}

int packet_get_line(struct strbuf *out,
	char **src_buf, size_t *src_len)
{
	int len;

	if (*src_len < 4)
		return -1;
	len = packet_length(*src_buf);
	if (len < 0)
		return -1;
	if (!len) {
		*src_buf += 4;
		*src_len -= 4;
		packet_trace("0000", 4, 0);
		return 0;
	}
	if (*src_len < len)
		return -2;

	*src_buf += 4;
	*src_len -= 4;
	len -= 4;

	strbuf_add(out, *src_buf, len);
	*src_buf += len;
	*src_len -= len;
	packet_trace(out->buf, out->len, 0);
	return len;
}
