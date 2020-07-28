#ifndef UNIX_SOCKET_H
#define UNIX_SOCKET_H

struct unix_stream_listen_opts {
	int listen_backlog_size;
};

#define UNIX_STREAM_LISTEN_OPTS_INIT { 0 }

int unix_stream_connect(const char *path);
int unix_stream_listen(const char *path,
		       const struct unix_stream_listen_opts *opts);

#endif /* UNIX_SOCKET_H */
