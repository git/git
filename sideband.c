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

#define PREFIX "remote:"

#define ANSI_SUFFIX "\033[K"
#define DUMB_SUFFIX "        "

#define FIX_SIZE 10  /* large enough for any of the above */

int recv_sideband(const char *me, int in_stream, int out, int err)
{
	unsigned pf = strlen(PREFIX);
	unsigned sf;
	char buf[LARGE_PACKET_MAX + 2*FIX_SIZE];
	char *suffix, *term;

	memcpy(buf, PREFIX, pf);
	term = getenv("TERM");
	if (term && strcmp(term, "dumb"))
		suffix = ANSI_SUFFIX;
	else
		suffix = DUMB_SUFFIX;
	sf = strlen(suffix);

	while (1) {
		int band, len;
		len = packet_read_line(in_stream, buf + pf, LARGE_PACKET_MAX);
		if (len == 0)
			break;
		if (len < 1) {
			len = sprintf(buf, "%s: protocol error: no band designator\n", me);
			safe_write(err, buf, len);
			return SIDEBAND_PROTOCOL_ERROR;
		}
		band = buf[pf] & 0xff;
		len--;
		switch (band) {
		case 3:
			buf[pf] = ' ';
			buf[pf+1+len] = '\n';
			safe_write(err, buf, pf+1+len+1);
			return SIDEBAND_REMOTE_ERROR;
		case 2:
			buf[pf] = ' ';
			len += pf+1;
			while (1) {
				int brk = pf+1;

				/* Break the buffer into separate lines. */
				while (brk < len) {
					brk++;
					if (buf[brk-1] == '\n' ||
					    buf[brk-1] == '\r')
						break;
				}

				/*
				 * Let's insert a suffix to clear the end
				 * of the screen line, but only if current
				 * line data actually contains something.
				 */
				if (brk > pf+1 + 1) {
					char save[FIX_SIZE];
					memcpy(save, buf + brk, sf);
					buf[brk + sf - 1] = buf[brk - 1];
					memcpy(buf + brk - 1, suffix, sf);
					safe_write(err, buf, brk + sf);
					memcpy(buf + brk, save, sf);
				} else
					safe_write(err, buf, brk);

				if (brk < len) {
					memmove(buf + pf+1, buf + brk, len - brk);
					len = len - brk + pf+1;
				} else
					break;
			}
			continue;
		case 1:
			safe_write(out, buf + pf+1, len);
			continue;
		default:
			len = sprintf(buf,
				      "%s: protocol error: bad band #%d\n",
				      me, band);
			safe_write(err, buf, len);
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
		sprintf(hdr, "%04x", n + 5);
		hdr[4] = band;
		safe_write(fd, hdr, 5);
		safe_write(fd, p, n);
		p += n;
		sz -= n;
	}
	return ssz;
}
