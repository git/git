#include "cache.h"
#include "pkt-line.h"
#include "sideband.h"

/*
 * Receive multiplexed output stream over git native protocol.
 * in_stream is the input stream from the remote, which carries data
 * in pkt_line format with band designator.  Demultiplex it into out
 * and err and return error appropriately.  Band #1 carries the
 * primary payload.  Things coming over band #2 is not necessarily
 * error; they are usually informative message on the standard error
 * stream, aka "verbose").  A message over band #3 is a signal that
 * the remote died unexpectedly.  A flush() concludes the stream.
 */

#define DISPLAY_PREFIX "remote: "

#define ANSI_SUFFIX "\033[K"
#define DUMB_SUFFIX "        "

int recv_sideband(const char *me, int in_stream, int out)
{
	const char *suffix;
	char buf[LARGE_PACKET_MAX + 1];
	struct strbuf outbuf = STRBUF_INIT;
	int retval = 0;

	if (isatty(2) && !is_terminal_dumb())
		suffix = ANSI_SUFFIX;
	else
		suffix = DUMB_SUFFIX;

	while (!retval) {
		const char *b, *brk;
		int band, len;
		len = packet_read(in_stream, NULL, NULL, buf, LARGE_PACKET_MAX, 0);
		if (len == 0)
			break;
		if (len < 1) {
			strbuf_addf(&outbuf,
				    "%s%s: protocol error: no band designator",
				    outbuf.len ? "\n" : "", me);
			retval = SIDEBAND_PROTOCOL_ERROR;
			break;
		}
		band = buf[0] & 0xff;
		buf[len] = '\0';
		len--;
		switch (band) {
		case 3:
			strbuf_addf(&outbuf, "%s%s%s", outbuf.len ? "\n" : "",
				    DISPLAY_PREFIX, buf + 1);
			retval = SIDEBAND_REMOTE_ERROR;
			break;
		case 2:
			b = buf + 1;

			/*
			 * Append a suffix to each nonempty line to clear the
			 * end of the screen line.
			 *
			 * The output is accumulated in a buffer and
			 * each line is printed to stderr using
			 * write(2) to ensure inter-process atomicity.
			 */
			while ((brk = strpbrk(b, "\n\r"))) {
				int linelen = brk - b;

				if (!outbuf.len)
					strbuf_addstr(&outbuf, DISPLAY_PREFIX);
				if (linelen > 0) {
					strbuf_addf(&outbuf, "%.*s%s%c",
						    linelen, b, suffix, *brk);
				} else {
					strbuf_addch(&outbuf, *brk);
				}
				xwrite(2, outbuf.buf, outbuf.len);
				strbuf_reset(&outbuf);

				b = brk + 1;
			}

			if (*b)
				strbuf_addf(&outbuf, "%s%s", outbuf.len ?
					    "" : DISPLAY_PREFIX, b);
			break;
		case 1:
			write_or_die(out, buf + 1, len);
			break;
		default:
			strbuf_addf(&outbuf, "%s%s: protocol error: bad band #%d",
				    outbuf.len ? "\n" : "", me, band);
			retval = SIDEBAND_PROTOCOL_ERROR;
			break;
		}
	}

	if (outbuf.len) {
		strbuf_addch(&outbuf, '\n');
		xwrite(2, outbuf.buf, outbuf.len);
	}
	strbuf_release(&outbuf);
	return retval;
}

/*
 * fd is connected to the remote side; send the sideband data
 * over multiplexed packet stream.
 */
void send_sideband(int fd, int band, const char *data, ssize_t sz, int packet_max)
{
	const char *p = data;

	while (sz) {
		unsigned n;
		char hdr[5];

		n = sz;
		if (packet_max - 5 < n)
			n = packet_max - 5;
		if (0 <= band) {
			xsnprintf(hdr, sizeof(hdr), "%04x", n + 5);
			hdr[4] = band;
			write_or_die(fd, hdr, 5);
		} else {
			xsnprintf(hdr, sizeof(hdr), "%04x", n + 4);
			write_or_die(fd, hdr, 4);
		}
		write_or_die(fd, p, n);
		p += n;
		sz -= n;
	}
}
