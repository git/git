#ifndef UNIX_SOCKET_H
#define UNIX_SOCKET_H

struct unix_stream_listen_opts {
	long timeout_ms;
	int listen_backlog_size;
	unsigned int disallow_chdir:1;
};

#define DEFAULT_UNIX_STREAM_LISTEN_TIMEOUT (100)
#define DEFAULT_UNIX_STREAM_LISTEN_BACKLOG (5)

#define UNIX_STREAM_LISTEN_OPTS_INIT \
{ \
	.timeout_ms = DEFAULT_UNIX_STREAM_LISTEN_TIMEOUT, \
	.listen_backlog_size = DEFAULT_UNIX_STREAM_LISTEN_BACKLOG, \
	.disallow_chdir = 0, \
}

int unix_stream_connect(const char *path, int disallow_chdir);
int unix_stream_listen(const char *path,
		       const struct unix_stream_listen_opts *opts);

struct unix_stream_server_socket {
	char *path_socket;
	struct stat st_socket;
	int fd_socket;
};

/*
 * Create a Unix Domain Socket at the given path under the protection
 * of a '.lock' lockfile.
 */
struct unix_stream_server_socket *unix_stream_server__listen_with_lock(
	const char *path,
	const struct unix_stream_listen_opts *opts);

/*
 * Close and delete the socket.
 */
void unix_stream_server__free(
	struct unix_stream_server_socket *server_socket);

/*
 * Return 1 if the inode of the pathname to our socket changes.
 */
int unix_stream_server__was_stolen(
	struct unix_stream_server_socket *server_socket);

#endif /* UNIX_SOCKET_H */
