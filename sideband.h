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

/*
 * Apply sideband configuration for the given URL. This should be called
 * when a transport is created to allow URL-specific configuration of
 * sideband behavior (e.g., sideband.<url>.allowControlCharacters).
 */
void sideband_apply_url_config(const char *url);

/*
 * Parse and set the sideband allow control characters configuration.
 * The var parameter should be the key name (without section prefix).
 * Returns 0 if the variable was recognized and handled, non-zero otherwise.
 */
int sideband_allow_control_characters_config(const char *var, const char *value);

#endif
