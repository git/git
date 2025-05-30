#ifndef CONNECT_H
#define CONNECT_H

#include "string-list.h"
#include "protocol.h"

#define CONNECT_VERBOSE       (1u << 0)
#define CONNECT_DIAG_URL      (1u << 1)
#define CONNECT_IPV4          (1u << 2)
#define CONNECT_IPV6          (1u << 3)
struct child_process *git_connect(int fd[2], const char *url, const char *name, const char *prog, int flags);
int finish_connect(struct child_process *conn);
int git_connection_is_socket(struct child_process *conn);
int server_supports(const char *feature);
int parse_feature_request(const char *features, const char *feature);
const char *server_feature_value(const char *feature, size_t *len_ret);
int url_is_local_not_ssh(const char *url);

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

void write_command_and_capabilities(struct strbuf *req_buf, const char *command,
									const struct string_list *server_options);

#endif
