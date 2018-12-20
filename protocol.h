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
enum protocol_version get_protocol_version_config(void);

/*
 * Register an allowable protocol version for a given operation. Registration
 * must occur before attempting to advertise a version to a server process.
 */
void register_allowed_protocol_version(enum protocol_version version);

/*
 * Register allowable protocol versions from the GIT_PROTOCOL environment var.
 */
void register_allowed_protocol_versions_from_env(void);

/*
 * Fill a strbuf with a version advertisement string suitable for use in the
 * GIT_PROTOCOL environment variable or similar version negotiation field.
 */
void get_client_protocol_version_advertisement(struct strbuf *advert);

/*
 * Used by a server to determine which protocol version should be used based on
 * a client's request, communicated via the 'GIT_PROTOCOL' environment variable
 * by setting appropriate values for the key 'version'.  If a client doesn't
 * request a particular protocol version, a default of 'protocol_v0' will be
 * used.
 */
enum protocol_version determine_protocol_version_server(void);

/*
 * Used by a client to determine which protocol version the server is speaking
 * based on the server's initial response.
 */
enum protocol_version determine_protocol_version_client(const char *server_response);

#endif /* PROTOCOL_H */
