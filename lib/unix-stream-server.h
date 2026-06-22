#ifndef UNIX_STREAM_SERVER_H
#define UNIX_STREAM_SERVER_H

#include "unix-socket.h"

struct unix_ss_socket {
	char *path_socket;
	struct stat st_socket;
	int fd_socket;
};

/*
 * Create a Unix Domain Socket at the given path under the protection
 * of a '.lock' lockfile.
 *
 * Returns 0 on success, -1 on error, -2 if socket is in use.
 */
int unix_ss_create(const char *path,
		   const struct unix_stream_listen_opts *opts,
		   long timeout_ms,
		   struct unix_ss_socket **server_socket);

/*
 * Close and delete the socket.
 */
void unix_ss_free(struct unix_ss_socket *server_socket);

/*
 * Return 1 if the inode of the pathname to our socket changes.
 */
int unix_ss_was_stolen(struct unix_ss_socket *server_socket);

#endif /* UNIX_STREAM_SERVER_H */
