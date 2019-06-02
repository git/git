#ifndef CONNECT_H
#define CONNECT_H

#include "protocol.h"

#define CONNECT_VERBOSE       (1u << 0)
#define CONNECT_DIAG_URL      (1u << 1)
#define CONNECT_IPV4          (1u << 2)
#define CONNECT_IPV6          (1u << 3)
struct child_process *git_connect(int fd[2], const char *url, const char *prog, int flags);
int finish_connect(struct child_process *conn);
int git_connection_is_socket(struct child_process *conn);
int server_supports(const char *feature);
int parse_feature_request(const char *features, const char *feature);
const char *server_feature_value(const char *feature, int *len_ret);
int url_is_local_not_ssh(const char *url);

struct packet_reader;
enum protocol_version discover_version(struct packet_reader *reader);

int server_supports_v2(const char *c, int die_on_error);
int server_supports_feature(const char *c, const char *feature,
			    int die_on_error);

#endif
