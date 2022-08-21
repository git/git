#ifndef GIT_SIMPLE_IPC_H
#define GIT_SIMPLE_IPC_H

/*
 * See Documentation/technical/api-simple-ipc.txt
 */

enum ipc_active_state {
	/*
	 * The pipe/socket exists and the daemon is waiting for connections.
	 */
	IPC_STATE__LISTENING = 0,

	/*
	 * The pipe/socket exists, but the daemon is not listening.
	 * Perhaps it is very busy.
	 * Perhaps the daemon died without deleting the path.
	 * Perhaps it is shutting down and draining existing clients.
	 * Perhaps it is dead, but other clients are lingering and
	 * still holding a reference to the pathname.
	 */
	IPC_STATE__NOT_LISTENING,

	/*
	 * The requested pathname is bogus and no amount of retries
	 * will fix that.
	 */
	IPC_STATE__INVALID_PATH,

	/*
	 * The requested pathname is not found.  This usually means
	 * that there is no daemon present.
	 */
	IPC_STATE__PATH_NOT_FOUND,

	IPC_STATE__OTHER_ERROR,
};

#ifdef SUPPORTS_SIMPLE_IPC
#include "pkt-line.h"

/*
 * Simple IPC Client Side API.
 */

struct ipc_client_connect_options {
	/*
	 * Spin under timeout if the server is running but can't
	 * accept our connection yet.  This should always be set
	 * unless you just want to poke the server and see if it
	 * is alive.
	 */
	unsigned int wait_if_busy:1;

	/*
	 * Spin under timeout if the pipe/socket is not yet present
	 * on the file system.  This is useful if we just started
	 * the service and need to wait for it to become ready.
	 */
	unsigned int wait_if_not_found:1;

	/*
	 * Disallow chdir() when creating a Unix domain socket.
	 */
	unsigned int uds_disallow_chdir:1;
};

#define IPC_CLIENT_CONNECT_OPTIONS_INIT { 0 }

/*
 * Determine if a server is listening on this named pipe or socket using
 * platform-specific logic.  This might just probe the filesystem or it
 * might make a trivial connection to the server using this pathname.
 */
enum ipc_active_state ipc_get_active_state(const char *path);

struct ipc_client_connection {
	int fd;
};

/*
 * Try to connect to the daemon on the named pipe or socket.
 *
 * Returns IPC_STATE__LISTENING and a connection handle.
 *
 * Otherwise, returns info to help decide whether to retry or to
 * spawn/respawn the server.
 */
enum ipc_active_state ipc_client_try_connect(
	const char *path,
	const struct ipc_client_connect_options *options,
	struct ipc_client_connection **p_connection);

void ipc_client_close_connection(struct ipc_client_connection *connection);

/*
 * Used by the client to synchronously send and receive a message with
 * the server on the provided client connection.
 *
 * Returns 0 when successful.
 *
 * Calls error() and returns non-zero otherwise.
 */
int ipc_client_send_command_to_connection(
	struct ipc_client_connection *connection,
	const char *message, size_t message_len,
	struct strbuf *answer);

/*
 * Used by the client to synchronously connect and send and receive a
 * message to the server listening at the given path.
 *
 * Returns 0 when successful.
 *
 * Calls error() and returns non-zero otherwise.
 */
int ipc_client_send_command(const char *path,
			    const struct ipc_client_connect_options *options,
			    const char *message, size_t message_len,
			    struct strbuf *answer);

/*
 * Simple IPC Server Side API.
 */

struct ipc_server_reply_data;

typedef int (ipc_server_reply_cb)(struct ipc_server_reply_data *,
				  const char *response,
				  size_t response_len);

/*
 * Prototype for an application-supplied callback to process incoming
 * client IPC messages and compose a reply.  The `application_cb` should
 * use the provided `reply_cb` and `reply_data` to send an IPC response
 * back to the client.  The `reply_cb` callback can be called multiple
 * times for chunking purposes.  A reply message is optional and may be
 * omitted if not necessary for the application.
 *
 * The return value from the application callback is ignored.
 * The value `SIMPLE_IPC_QUIT` can be used to shutdown the server.
 */
typedef int (ipc_server_application_cb)(void *application_data,
					const char *request,
					size_t request_len,
					ipc_server_reply_cb *reply_cb,
					struct ipc_server_reply_data *reply_data);

#define SIMPLE_IPC_QUIT -2

/*
 * Opaque instance data to represent an IPC server instance.
 */
struct ipc_server_data;

/*
 * Control parameters for the IPC server instance.
 * Use this to hide platform-specific settings.
 */
struct ipc_server_opts
{
	int nr_threads;

	/*
	 * Disallow chdir() when creating a Unix domain socket.
	 */
	unsigned int uds_disallow_chdir:1;
};

/*
 * Start an IPC server instance in one or more background threads
 * and return a handle to the pool.
 *
 * Returns 0 if the asynchronous server pool was started successfully.
 * Returns -1 if not.
 * Returns -2 if we could not startup because another server is using
 * the socket or named pipe.
 *
 * When a client IPC message is received, the `application_cb` will be
 * called (possibly on a random thread) to handle the message and
 * optionally compose a reply message.
 */
int ipc_server_run_async(struct ipc_server_data **returned_server_data,
			 const char *path, const struct ipc_server_opts *opts,
			 ipc_server_application_cb *application_cb,
			 void *application_data);

/*
 * Gently signal the IPC server pool to shutdown.  No new client
 * connections will be accepted, but existing connections will be
 * allowed to complete.
 */
int ipc_server_stop_async(struct ipc_server_data *server_data);

/*
 * Block the calling thread until all threads in the IPC server pool
 * have completed and been joined.
 */
int ipc_server_await(struct ipc_server_data *server_data);

/*
 * Close and free all resource handles associated with the IPC server
 * pool.
 */
void ipc_server_free(struct ipc_server_data *server_data);

/*
 * Run an IPC server instance and block the calling thread of the
 * current process.  It does not return until the IPC server has
 * either shutdown or had an unrecoverable error.
 *
 * The IPC server handles incoming IPC messages from client processes
 * and may use one or more background threads as necessary.
 *
 * Returns 0 after the server has completed successfully.
 * Returns -1 if the server cannot be started.
 * Returns -2 if we could not startup because another server is using
 * the socket or named pipe.
 *
 * When a client IPC message is received, the `application_cb` will be
 * called (possibly on a random thread) to handle the message and
 * optionally compose a reply message.
 *
 * Note that `ipc_server_run()` is a synchronous wrapper around the
 * above asynchronous routines.  It effectively hides all of the
 * server state and thread details from the caller and presents a
 * simple synchronous interface.
 */
int ipc_server_run(const char *path, const struct ipc_server_opts *opts,
		   ipc_server_application_cb *application_cb,
		   void *application_data);

#endif /* SUPPORTS_SIMPLE_IPC */
#endif /* GIT_SIMPLE_IPC_H */
