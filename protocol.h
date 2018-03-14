#ifndef PROTOCOL_H
#define PROTOCOL_H

enum protocol_version {
	protocol_unknown_version = -1,
	protocol_v0 = 0,
	protocol_v1 = 1,
	protocol_v2 = 2,
};

/*
 * Used by a client to determine which protocol version to request be used when
 * communicating with a server, reflecting the configured value of the
 * 'protocol.version' config.  If unconfigured, a value of 'protocol_v0' is
 * returned.
 */
extern enum protocol_version get_protocol_version_config(void);

/*
 * Used by a server to determine which protocol version should be used based on
 * a client's request, communicated via the 'GIT_PROTOCOL' environment variable
 * by setting appropriate values for the key 'version'.  If a client doesn't
 * request a particular protocol version, a default of 'protocol_v0' will be
 * used.
 */
extern enum protocol_version determine_protocol_version_server(void);

/*
 * Used by a client to determine which protocol version the server is speaking
 * based on the server's initial response.
 */
extern enum protocol_version determine_protocol_version_client(const char *server_response);

#endif /* PROTOCOL_H */
