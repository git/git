#ifndef UNIX_SOCKET_H
#define UNIX_SOCKET_H

struct unix_stream_listen_opts {
	int listen_backlog_size;
};

#define DEFAULT_UNIX_STREAM_LISTEN_BACKLOG (5)

#define UNIX_STREAM_LISTEN_OPTS_INIT \
{ \
	.listen_backlog_size = DEFAULT_UNIX_STREAM_LISTEN_BACKLOG, \
}

int unix_stream_connect(const char *path);
int unix_stream_listen(const char *path,
		       const struct unix_stream_listen_opts *opts);

#endif /* UNIX_SOCKET_H */
