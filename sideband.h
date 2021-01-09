#ifndef SIDEBAND_H
#define SIDEBAND_H

enum sideband_type {
	SIDEBAND_PROTOCOL_ERROR = -2,
	SIDEBAND_REMOTE_ERROR = -1,
	SIDEBAND_FLUSH = 0,
	SIDEBAND_PRIMARY = 1
};

/*
 * Inspects a multiplexed packet read from the remote. If this packet is a
 * progress packet and thus should not be processed by the caller, returns 0.
 * Otherwise, returns 1, releases scratch, and sets sideband_type.
 *
 * If this packet is SIDEBAND_PROTOCOL_ERROR, SIDEBAND_REMOTE_ERROR, or a
 * progress packet, also prints a message to stderr.
 *
 * scratch must be a struct strbuf allocated by the caller. It is used to store
 * progress messages split across multiple packets.
 *
 * The "status" parameter is a pkt-line response as returned by
 * packet_read_with_status() (e.g., PACKET_READ_NORMAL).
 */
int demultiplex_sideband(const char *me, int status,
			 char *buf, int len,
			 int die_on_error,
			 struct strbuf *scratch,
			 enum sideband_type *sideband_type);

void send_sideband(int fd, int band, const char *data, ssize_t sz, int packet_max);

#endif
