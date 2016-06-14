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

#define PREFIX "remote: "

#define ANSI_SUFFIX "\033[K"
#define DUMB_SUFFIX "        "

int recv_sideband(const char *me, int in_stream, int out)
{
	const char *term;
	const char *prefix = PREFIX, *suffix;
	char buf[LARGE_PACKET_MAX + 1];
	const char *b, *brk;

	term = getenv("TERM");
	if (isatty(2) && term && strcmp(term, "dumb"))
		suffix = ANSI_SUFFIX;
	else
		suffix = DUMB_SUFFIX;

	while (1) {
		int band, len;
		len = packet_read(in_stream, NULL, NULL, buf, LARGE_PACKET_MAX, 0);
		if (len == 0)
			break;
		if (len < 1) {
			fprintf(stderr, "%s: protocol error: no band designator\n", me);
			return SIDEBAND_PROTOCOL_ERROR;
		}
		band = buf[0] & 0xff;
		buf[len] = '\0';
		len--;
		switch (band) {
		case 3:
			fprintf(stderr, "%s%s\n", PREFIX, buf + 1);
			return SIDEBAND_REMOTE_ERROR;
		case 2:
			b = buf + 1;

			/*
			 * Append a suffix to each nonempty line to clear the
			 * end of the screen line.
			 */
			while ((brk = strpbrk(b, "\n\r"))) {
				int linelen = brk - b;

				if (linelen > 0) {
					fprintf(stderr, "%s%.*s%s%c", prefix,
						linelen, b, suffix, *brk);
				} else {
					fprintf(stderr, "%s%c", prefix, *brk);
				}

				b = brk + 1;
				prefix = PREFIX;
			}

			if (*b) {
				fprintf(stderr, "%s%s", prefix, b);
				/* Incomplete line, skip the next prefix. */
				prefix = "";
			}
			continue;
		case 1:
			write_or_die(out, buf + 1, len);
			continue;
		default:
			fprintf(stderr, "%s: protocol error: bad band #%d\n",
				me, band);
			return SIDEBAND_PROTOCOL_ERROR;
		}
	}
	return 0;
}

/*
 * fd is connected to the remote side; send the sideband data
 * over multiplexed packet stream.
 */
ssize_t send_sideband(int fd, int band, const char *data, ssize_t sz, int packet_max)
{
	ssize_t ssz = sz;
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
	return ssz;
}
