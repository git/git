#include "cache.h"
#include "lockfile.h"
#include "unix-socket.h"

static int chdir_len(const char *orig, int len)
{
	char *path = xmemdupz(orig, len);
	int r = chdir(path);
	free(path);
	return r;
}

struct unix_sockaddr_context {
	char *orig_dir;
};

static void unix_sockaddr_cleanup(struct unix_sockaddr_context *ctx)
{
	if (!ctx->orig_dir)
		return;
	/*
	 * If we fail, we can't just return an error, since we have
	 * moved the cwd of the whole process, which could confuse calling
	 * code.  We are better off to just die.
	 */
	if (chdir(ctx->orig_dir) < 0)
		die("unable to restore original working directory");
	free(ctx->orig_dir);
}

static int unix_sockaddr_init(struct sockaddr_un *sa, const char *path,
			      struct unix_sockaddr_context *ctx,
			      int disallow_chdir)
{
	int size = strlen(path) + 1;

	ctx->orig_dir = NULL;
	if (size > sizeof(sa->sun_path)) {
		const char *slash;
		const char *dir;
		struct strbuf cwd = STRBUF_INIT;

		if (disallow_chdir) {
			errno = ENAMETOOLONG;
			return -1;
		}

		slash = find_last_dir_sep(path);
		if (!slash) {
			errno = ENAMETOOLONG;
			return -1;
		}

		dir = path;
		path = slash + 1;
		size = strlen(path) + 1;
		if (size > sizeof(sa->sun_path)) {
			errno = ENAMETOOLONG;
			return -1;
		}
		if (strbuf_getcwd(&cwd))
			return -1;
		ctx->orig_dir = strbuf_detach(&cwd, NULL);
		if (chdir_len(dir, slash - dir) < 0)
			return -1;
	}

	memset(sa, 0, sizeof(*sa));
	sa->sun_family = AF_UNIX;
	memcpy(sa->sun_path, path, size);
	return 0;
}

int unix_stream_connect(const char *path, int disallow_chdir)
{
	int fd = -1, saved_errno;
	struct sockaddr_un sa;
	struct unix_sockaddr_context ctx;

	if (unix_sockaddr_init(&sa, path, &ctx, disallow_chdir) < 0)
		return -1;
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto fail;

	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
		goto fail;
	unix_sockaddr_cleanup(&ctx);
	return fd;

fail:
	saved_errno = errno;
	if (fd != -1)
		close(fd);
	unix_sockaddr_cleanup(&ctx);
	errno = saved_errno;
	return -1;
}

int unix_stream_listen(const char *path,
		       const struct unix_stream_listen_opts *opts)
{
	int fd = -1, saved_errno;
	int backlog;
	struct sockaddr_un sa;
	struct unix_sockaddr_context ctx;

	unlink(path);

	if (unix_sockaddr_init(&sa, path, &ctx, opts->disallow_chdir) < 0)
		return -1;
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		goto fail;

	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
		goto fail;

	backlog = opts->listen_backlog_size;
	if (backlog <= 0)
		backlog = DEFAULT_UNIX_STREAM_LISTEN_BACKLOG;
	if (listen(fd, backlog) < 0)
		goto fail;

	unix_sockaddr_cleanup(&ctx);
	return fd;

fail:
	saved_errno = errno;
	if (fd != -1)
		close(fd);
	unix_sockaddr_cleanup(&ctx);
	errno = saved_errno;
	return -1;
}

static int is_another_server_alive(const char *path,
				   const struct unix_stream_listen_opts *opts)
{
	struct stat st;
	int fd;

	if (!lstat(path, &st) && S_ISSOCK(st.st_mode)) {
		/*
		 * A socket-inode exists on disk at `path`, but we
		 * don't know whether it belongs to an active server
		 * or whether the last server died without cleaning
		 * up.
		 *
		 * Poke it with a trivial connection to try to find
		 * out.
		 */
		fd = unix_stream_connect(path, opts->disallow_chdir);
		if (fd >= 0) {
			close(fd);
			return 1;
		}
	}

	return 0;
}

struct unix_stream_server_socket *unix_stream_server__listen_with_lock(
	const char *path,
	const struct unix_stream_listen_opts *opts)
{
	struct lock_file lock = LOCK_INIT;
	int fd_socket;
	struct unix_stream_server_socket *server_socket;

	/*
	 * Create a lock at "<path>.lock" if we can.
	 */
	if (hold_lock_file_for_update_timeout(&lock, path, 0,
					      opts->timeout_ms) < 0) {
		error_errno(_("could not lock listener socket '%s'"), path);
		return NULL;
	}

	/*
	 * If another server is listening on "<path>" give up.  We do not
	 * want to create a socket and steal future connections from them.
	 */
	if (is_another_server_alive(path, opts)) {
		errno = EADDRINUSE;
		error_errno(_("listener socket already in use '%s'"), path);
		rollback_lock_file(&lock);
		return NULL;
	}

	/*
	 * Create and bind to a Unix domain socket at "<path>".
	 */
	fd_socket = unix_stream_listen(path, opts);
	if (fd_socket < 0) {
		error_errno(_("could not create listener socket '%s'"), path);
		rollback_lock_file(&lock);
		return NULL;
	}

	server_socket = xcalloc(1, sizeof(*server_socket));
	server_socket->path_socket = strdup(path);
	server_socket->fd_socket = fd_socket;
	lstat(path, &server_socket->st_socket);

	/*
	 * Always rollback (just delete) "<path>.lock" because we already created
	 * "<path>" as a socket and do not want to commit_lock to do the atomic
	 * rename trick.
	 */
	rollback_lock_file(&lock);

	return server_socket;
}

void unix_stream_server__free(
	struct unix_stream_server_socket *server_socket)
{
	if (!server_socket)
		return;

	if (server_socket->fd_socket >= 0) {
		if (!unix_stream_server__was_stolen(server_socket))
			unlink(server_socket->path_socket);
		close(server_socket->fd_socket);
	}

	free(server_socket->path_socket);
	free(server_socket);
}

int unix_stream_server__was_stolen(
	struct unix_stream_server_socket *server_socket)
{
	struct stat st_now;

	if (!server_socket)
		return 0;

	if (lstat(server_socket->path_socket, &st_now) == -1)
		return 1;

	if (st_now.st_ino != server_socket->st_socket.st_ino)
		return 1;

	/* We might also consider the ctime on some platforms. */

	return 0;
}
