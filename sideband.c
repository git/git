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
int recv_sideband(const char *me, int in_stream, int out, int err, char *buf, int bufsz)
{
	while (1) {
		int len = packet_read_line(in_stream, buf, bufsz);
		if (len == 0)
			break;
		if (len < 1) {
			len = sprintf(buf, "%s: protocol error: no band designator\n", me);
			safe_write(err, buf, len);
			return SIDEBAND_PROTOCOL_ERROR;
		}
		len--;
		switch (buf[0] & 0xFF) {
		case 3:
			safe_write(err, "remote: ", 8);
			safe_write(err, buf+1, len);
			safe_write(err, "\n", 1);
			return SIDEBAND_REMOTE_ERROR;
		case 2:
			safe_write(err, "remote: ", 8);
			safe_write(err, buf+1, len);
			continue;
		case 1:
			safe_write(out, buf+1, len);
			continue;
		default:
			len = sprintf(buf + 1,
				      "%s: protocol error: bad band #%d\n",
				      me, buf[0] & 0xFF);
			safe_write(err, buf+1, len);
			return SIDEBAND_PROTOCOL_ERROR;
		}
	}
	return 0;
}
