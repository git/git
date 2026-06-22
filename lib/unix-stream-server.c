#include "git-compat-util.h"
#include "lockfile.h"
#include "unix-socket.h"
#include "unix-stream-server.h"

#define DEFAULT_LOCK_TIMEOUT (100)

/*
 * Try to connect to a unix domain socket at `path` (if it exists) and
 * see if there is a server listening.
 *
 * We don't know if the socket exists, whether a server died and
 * failed to cleanup, or whether we have a live server listening, so
 * we "poke" it.
 *
 * We immediately hangup without sending/receiving any data because we
 * don't know anything about the protocol spoken and don't want to
 * block while writing/reading data.  It is sufficient to just know
 * that someone is listening.
 */
static int is_another_server_alive(const char *path,
				   const struct unix_stream_listen_opts *opts)
{
	int fd = unix_stream_connect(path, opts->disallow_chdir);
	if (fd >= 0) {
		close(fd);
		return 1;
	}

	return 0;
}

int unix_ss_create(const char *path,
		   const struct unix_stream_listen_opts *opts,
		   long timeout_ms,
		   struct unix_ss_socket **new_server_socket)
{
	struct lock_file lock = LOCK_INIT;
	int fd_socket;
	struct unix_ss_socket *server_socket;

	*new_server_socket = NULL;

	if (timeout_ms < 0)
		timeout_ms = DEFAULT_LOCK_TIMEOUT;

	/*
	 * Create a lock at "<path>.lock" if we can.
	 */
	if (hold_lock_file_for_update_timeout(&lock, path, 0, timeout_ms) < 0)
		return -1;

	/*
	 * If another server is listening on "<path>" give up.  We do not
	 * want to create a socket and steal future connections from them.
	 */
	if (is_another_server_alive(path, opts)) {
		rollback_lock_file(&lock);
		errno = EADDRINUSE;
		return -2;
	}

	/*
	 * Create and bind to a Unix domain socket at "<path>".
	 */
	fd_socket = unix_stream_listen(path, opts);
	if (fd_socket < 0) {
		int saved_errno = errno;
		rollback_lock_file(&lock);
		errno = saved_errno;
		return -1;
	}

	server_socket = xcalloc(1, sizeof(*server_socket));
	server_socket->path_socket = strdup(path);
	server_socket->fd_socket = fd_socket;
	lstat(path, &server_socket->st_socket);

	*new_server_socket = server_socket;

	/*
	 * Always rollback (just delete) "<path>.lock" because we already created
	 * "<path>" as a socket and do not want to commit_lock to do the atomic
	 * rename trick.
	 */
	rollback_lock_file(&lock);

	return 0;
}

void unix_ss_free(struct unix_ss_socket *server_socket)
{
	if (!server_socket)
		return;

	if (server_socket->fd_socket >= 0) {
		if (!unix_ss_was_stolen(server_socket))
			unlink(server_socket->path_socket);
		close(server_socket->fd_socket);
	}

	free(server_socket->path_socket);
	free(server_socket);
}

int unix_ss_was_stolen(struct unix_ss_socket *server_socket)
{
	struct stat st_now;

	if (!server_socket)
		return 0;

	if (lstat(server_socket->path_socket, &st_now) == -1)
		return 1;

	if (st_now.st_ino != server_socket->st_socket.st_ino)
		return 1;
	if (st_now.st_dev != server_socket->st_socket.st_dev)
		return 1;

	if (!S_ISSOCK(st_now.st_mode))
		return 1;

	return 0;
}
