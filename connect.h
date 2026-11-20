#ifndef CONNECT_H
#define CONNECT_H

#include "protocol.h"

#define CONNECT_VERBOSE       (1u << 0)
#define CONNECT_DIAG_URL      (1u << 1)
#define CONNECT_IPV4          (1u << 2)
#define CONNECT_IPV6          (1u << 3)
enum git_connect_service {
    GIT_CONNECT_UPLOAD_PACK,
    GIT_CONNECT_RECEIVE_PACK,
    GIT_CONNECT_UPLOAD_ARCHIVE,
};
struct child_process *git_connect(int fd[2], const char *url, enum git_connect_service, const char *prog, int flags);
int finish_connect(struct child_process *conn);
int git_connection_is_socket(struct child_process *conn);
int server_supports(const char *feature);
int parse_feature_request(const char *features, const char *feature);
const char *server_feature_value(const char *feature, size_t *len_ret);

struct packet_reader;
enum protocol_version discover_version(struct packet_reader *reader);

int server_supports_hash(const char *desired, int *feature_supported);
const char *parse_feature_value(const char *feature_list, const char *feature, size_t *lenp, size_t *offset);
int server_supports_v2(const char *c);
void ensure_server_supports_v2(const char *c);
int server_feature_v2(const char *c, const char **v);
int server_supports_feature(const char *c, const char *feature,
			    int die_on_error);

void check_stateless_delimiter(int stateless_rpc,
			       struct packet_reader *reader,
			       const char *error);

struct string_list;
struct strvec;

/*
 * Append a "server-option=<value>" capability to `capabilities` for
 * each item in `server_options`, which may be NULL.
 */
void add_server_options(struct strvec *capabilities,
			const struct string_list *server_options);

/*
 * Write a protocol v2 command request, along with the capability
 * advertisements, into req_buf. The "agent", "session-id" and
 * "object-format" capabilities are handled here, and must not be in
 * `capabilities`. `capabilities` contains complete "key[=value]"
 * lines and must not be NULL. The keys in `capabilities` must have
 * been advertised by the server, otherwise we die(). If `reader` is
 * not NULL, its `hash_algo` member is set from the "object-format"
 * capability sent by the server (or to SHA-1 if the server didn't
 * send one). Otherwise that capability is checked against
 * `the_hash_algo`, and we die() if they don't match.
 */
void write_command_and_capabilities(struct strbuf *req_buf, const char *command,
				    const struct strvec *capabilities,
				    struct packet_reader *reader);

#endif
