#include "cache.h"
#include "simple-ipc.h"
#include "strbuf.h"
#include "pkt-line.h"
#include "thread-utils.h"
#include "accctrl.h"
#include "aclapi.h"

#ifndef SUPPORTS_SIMPLE_IPC
/*
 * This source file should only be compiled when Simple IPC is supported.
 * See the top-level Makefile.
 */
#error SUPPORTS_SIMPLE_IPC not defined
#endif

static int initialize_pipe_name(const char *path, wchar_t *wpath, size_t alloc)
{
	int off = 0;
	struct strbuf realpath = STRBUF_INIT;

	if (!strbuf_realpath(&realpath, path, 0))
		return -1;

	off = swprintf(wpath, alloc, L"\\\\.\\pipe\\");
	if (xutftowcs(wpath + off, realpath.buf, alloc - off) < 0)
		return -1;

	/* Handle drive prefix */
	if (wpath[off] && wpath[off + 1] == L':') {
		wpath[off + 1] = L'_';
		off += 2;
	}

	for (; wpath[off]; off++)
		if (wpath[off] == L'/')
			wpath[off] = L'\\';

	strbuf_release(&realpath);
	return 0;
}

static enum ipc_active_state get_active_state(wchar_t *pipe_path)
{
	if (WaitNamedPipeW(pipe_path, NMPWAIT_USE_DEFAULT_WAIT))
		return IPC_STATE__LISTENING;

	if (GetLastError() == ERROR_SEM_TIMEOUT)
		return IPC_STATE__NOT_LISTENING;

	if (GetLastError() == ERROR_FILE_NOT_FOUND)
		return IPC_STATE__PATH_NOT_FOUND;

	trace2_data_intmax("ipc-debug", NULL, "getstate/waitpipe/gle",
			   (intmax_t)GetLastError());

	return IPC_STATE__OTHER_ERROR;
}

enum ipc_active_state ipc_get_active_state(const char *path)
{
	wchar_t pipe_path[MAX_PATH];

	if (initialize_pipe_name(path, pipe_path, ARRAY_SIZE(pipe_path)) < 0)
		return IPC_STATE__INVALID_PATH;

	return get_active_state(pipe_path);
}

#define WAIT_STEP_MS (50)

static enum ipc_active_state connect_to_server(
	const wchar_t *wpath,
	DWORD timeout_ms,
	const struct ipc_client_connect_options *options,
	int *pfd)
{
	DWORD t_start_ms, t_waited_ms;
	DWORD step_ms;
	HANDLE hPipe = INVALID_HANDLE_VALUE;
	DWORD mode = PIPE_READMODE_BYTE;
	DWORD gle;

	*pfd = -1;

	for (;;) {
		hPipe = CreateFileW(wpath, GENERIC_READ | GENERIC_WRITE,
				    0, NULL, OPEN_EXISTING, 0, NULL);
		if (hPipe != INVALID_HANDLE_VALUE)
			break;

		gle = GetLastError();

		switch (gle) {
		case ERROR_FILE_NOT_FOUND:
			if (!options->wait_if_not_found)
				return IPC_STATE__PATH_NOT_FOUND;
			if (!timeout_ms)
				return IPC_STATE__PATH_NOT_FOUND;

			step_ms = (timeout_ms < WAIT_STEP_MS) ?
				timeout_ms : WAIT_STEP_MS;
			sleep_millisec(step_ms);

			timeout_ms -= step_ms;
			break; /* try again */

		case ERROR_PIPE_BUSY:
			if (!options->wait_if_busy)
				return IPC_STATE__NOT_LISTENING;
			if (!timeout_ms)
				return IPC_STATE__NOT_LISTENING;

			t_start_ms = (DWORD)(getnanotime() / 1000000);

			if (!WaitNamedPipeW(wpath, timeout_ms)) {
				DWORD gleWait = GetLastError();

				if (gleWait == ERROR_SEM_TIMEOUT)
					return IPC_STATE__NOT_LISTENING;

				trace2_data_intmax("ipc-debug", NULL,
						   "connect/waitpipe/gle",
						   (intmax_t)gleWait);

				return IPC_STATE__OTHER_ERROR;
			}

			/*
			 * A pipe server instance became available.
			 * Race other client processes to connect to
			 * it.
			 *
			 * But first decrement our overall timeout so
			 * that we don't starve if we keep losing the
			 * race.  But also guard against special
			 * NPMWAIT_ values (0 and -1).
			 */
			t_waited_ms = (DWORD)(getnanotime() / 1000000) - t_start_ms;
			if (t_waited_ms < timeout_ms)
				timeout_ms -= t_waited_ms;
			else
				timeout_ms = 1;
			break; /* try again */

		default:
			trace2_data_intmax("ipc-debug", NULL,
					   "connect/createfile/gle",
					   (intmax_t)gle);

			return IPC_STATE__OTHER_ERROR;
		}
	}

	if (!SetNamedPipeHandleState(hPipe, &mode, NULL, NULL)) {
		gle = GetLastError();
		trace2_data_intmax("ipc-debug", NULL,
				   "connect/setpipestate/gle",
				   (intmax_t)gle);

		CloseHandle(hPipe);
		return IPC_STATE__OTHER_ERROR;
	}

	*pfd = _open_osfhandle((intptr_t)hPipe, O_RDWR|O_BINARY);
	if (*pfd < 0) {
		gle = GetLastError();
		trace2_data_intmax("ipc-debug", NULL,
				   "connect/openosfhandle/gle",
				   (intmax_t)gle);

		CloseHandle(hPipe);
		return IPC_STATE__OTHER_ERROR;
	}

	/* fd now owns hPipe */

	return IPC_STATE__LISTENING;
}

/*
 * The default connection timeout for Windows clients.
 *
 * This is not currently part of the ipc_ API (nor the config settings)
 * because of differences between Windows and other platforms.
 *
 * This value was chosen at random.
 */
#define WINDOWS_CONNECTION_TIMEOUT_MS (30000)

enum ipc_active_state ipc_client_try_connect(
	const char *path,
	const struct ipc_client_connect_options *options,
	struct ipc_client_connection **p_connection)
{
	wchar_t wpath[MAX_PATH];
	enum ipc_active_state state = IPC_STATE__OTHER_ERROR;
	int fd = -1;

	*p_connection = NULL;

	trace2_region_enter("ipc-client", "try-connect", NULL);
	trace2_data_string("ipc-client", NULL, "try-connect/path", path);

	if (initialize_pipe_name(path, wpath, ARRAY_SIZE(wpath)) < 0)
		state = IPC_STATE__INVALID_PATH;
	else
		state = connect_to_server(wpath, WINDOWS_CONNECTION_TIMEOUT_MS,
					  options, &fd);

	trace2_data_intmax("ipc-client", NULL, "try-connect/state",
			   (intmax_t)state);
	trace2_region_leave("ipc-client", "try-connect", NULL);

	if (state == IPC_STATE__LISTENING) {
		(*p_connection) = xcalloc(1, sizeof(struct ipc_client_connection));
		(*p_connection)->fd = fd;
	}

	return state;
}

void ipc_client_close_connection(struct ipc_client_connection *connection)
{
	if (!connection)
		return;

	if (connection->fd != -1)
		close(connection->fd);

	free(connection);
}

int ipc_client_send_command_to_connection(
	struct ipc_client_connection *connection,
	const char *message, size_t message_len,
	struct strbuf *answer)
{
	int ret = 0;

	strbuf_setlen(answer, 0);

	trace2_region_enter("ipc-client", "send-command", NULL);

	if (write_packetized_from_buf_no_flush(message, message_len,
					       connection->fd) < 0 ||
	    packet_flush_gently(connection->fd) < 0) {
		ret = error(_("could not send IPC command"));
		goto done;
	}

	FlushFileBuffers((HANDLE)_get_osfhandle(connection->fd));

	if (read_packetized_to_strbuf(
		    connection->fd, answer,
		    PACKET_READ_GENTLE_ON_EOF | PACKET_READ_GENTLE_ON_READ_ERROR) < 0) {
		ret = error(_("could not read IPC response"));
		goto done;
	}

done:
	trace2_region_leave("ipc-client", "send-command", NULL);
	return ret;
}

int ipc_client_send_command(const char *path,
			    const struct ipc_client_connect_options *options,
			    const char *message, size_t message_len,
			    struct strbuf *response)
{
	int ret = -1;
	enum ipc_active_state state;
	struct ipc_client_connection *connection = NULL;

	state = ipc_client_try_connect(path, options, &connection);

	if (state != IPC_STATE__LISTENING)
		return ret;

	ret = ipc_client_send_command_to_connection(connection,
						    message, message_len,
						    response);

	ipc_client_close_connection(connection);

	return ret;
}

/*
 * Duplicate the given pipe handle and wrap it in a file descriptor so
 * that we can use pkt-line on it.
 */
static int dup_fd_from_pipe(const HANDLE pipe)
{
	HANDLE process = GetCurrentProcess();
	HANDLE handle;
	int fd;

	if (!DuplicateHandle(process, pipe, process, &handle, 0, FALSE,
			     DUPLICATE_SAME_ACCESS)) {
		errno = err_win_to_posix(GetLastError());
		return -1;
	}

	fd = _open_osfhandle((intptr_t)handle, O_RDWR|O_BINARY);
	if (fd < 0) {
		errno = err_win_to_posix(GetLastError());
		CloseHandle(handle);
		return -1;
	}

	/*
	 * `handle` is now owned by `fd` and will be automatically closed
	 * when the descriptor is closed.
	 */

	return fd;
}

/*
 * Magic numbers used to annotate callback instance data.
 * These are used to help guard against accidentally passing the
 * wrong instance data across multiple levels of callbacks (which
 * is easy to do if there are `void*` arguments).
 */
enum magic {
	MAGIC_SERVER_REPLY_DATA,
	MAGIC_SERVER_THREAD_DATA,
	MAGIC_SERVER_DATA,
};

struct ipc_server_reply_data {
	enum magic magic;
	int fd;
	struct ipc_server_thread_data *server_thread_data;
};

struct ipc_server_thread_data {
	enum magic magic;
	struct ipc_server_thread_data *next_thread;
	struct ipc_server_data *server_data;
	pthread_t pthread_id;
	HANDLE hPipe;
};

/*
 * On Windows, the conceptual "ipc-server" is implemented as a pool of
 * n idential/peer "server-thread" threads.  That is, there is no
 * hierarchy of threads; and therefore no controller thread managing
 * the pool.  Each thread has an independent handle to the named pipe,
 * receives incoming connections, processes the client, and re-uses
 * the pipe for the next client connection.
 *
 * Therefore, the "ipc-server" only needs to maintain a list of the
 * spawned threads for eventual "join" purposes.
 *
 * A single "stop-event" is visible to all of the server threads to
 * tell them to shutdown (when idle).
 */
struct ipc_server_data {
	enum magic magic;
	ipc_server_application_cb *application_cb;
	void *application_data;
	struct strbuf buf_path;
	wchar_t wpath[MAX_PATH];

	HANDLE hEventStopRequested;
	struct ipc_server_thread_data *thread_list;
	int is_stopped;
};

enum connect_result {
	CR_CONNECTED = 0,
	CR_CONNECT_PENDING,
	CR_CONNECT_ERROR,
	CR_WAIT_ERROR,
	CR_SHUTDOWN,
};

static enum connect_result queue_overlapped_connect(
	struct ipc_server_thread_data *server_thread_data,
	OVERLAPPED *lpo)
{
	if (ConnectNamedPipe(server_thread_data->hPipe, lpo))
		goto failed;

	switch (GetLastError()) {
	case ERROR_IO_PENDING:
		return CR_CONNECT_PENDING;

	case ERROR_PIPE_CONNECTED:
		SetEvent(lpo->hEvent);
		return CR_CONNECTED;

	default:
		break;
	}

failed:
	error(_("ConnectNamedPipe failed for '%s' (%lu)"),
	      server_thread_data->server_data->buf_path.buf,
	      GetLastError());
	return CR_CONNECT_ERROR;
}

/*
 * Use Windows Overlapped IO to wait for a connection or for our event
 * to be signalled.
 */
static enum connect_result wait_for_connection(
	struct ipc_server_thread_data *server_thread_data,
	OVERLAPPED *lpo)
{
	enum connect_result r;
	HANDLE waitHandles[2];
	DWORD dwWaitResult;

	r = queue_overlapped_connect(server_thread_data, lpo);
	if (r != CR_CONNECT_PENDING)
		return r;

	waitHandles[0] = server_thread_data->server_data->hEventStopRequested;
	waitHandles[1] = lpo->hEvent;

	dwWaitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
	switch (dwWaitResult) {
	case WAIT_OBJECT_0 + 0:
		return CR_SHUTDOWN;

	case WAIT_OBJECT_0 + 1:
		ResetEvent(lpo->hEvent);
		return CR_CONNECTED;

	default:
		return CR_WAIT_ERROR;
	}
}

/*
 * Forward declare our reply callback function so that any compiler
 * errors are reported when we actually define the function (in addition
 * to any errors reported when we try to pass this callback function as
 * a parameter in a function call).  The former are easier to understand.
 */
static ipc_server_reply_cb do_io_reply_callback;

/*
 * Relay application's response message to the client process.
 * (We do not flush at this point because we allow the caller
 * to chunk data to the client thru us.)
 */
static int do_io_reply_callback(struct ipc_server_reply_data *reply_data,
		       const char *response, size_t response_len)
{
	if (reply_data->magic != MAGIC_SERVER_REPLY_DATA)
		BUG("reply_cb called with wrong instance data");

	return write_packetized_from_buf_no_flush(response, response_len,
						  reply_data->fd);
}

/*
 * Receive the request/command from the client and pass it to the
 * registered request-callback.  The request-callback will compose
 * a response and call our reply-callback to send it to the client.
 *
 * Simple-IPC only contains one round trip, so we flush and close
 * here after the response.
 */
static int do_io(struct ipc_server_thread_data *server_thread_data)
{
	struct strbuf buf = STRBUF_INIT;
	struct ipc_server_reply_data reply_data;
	int ret = 0;

	reply_data.magic = MAGIC_SERVER_REPLY_DATA;
	reply_data.server_thread_data = server_thread_data;

	reply_data.fd = dup_fd_from_pipe(server_thread_data->hPipe);
	if (reply_data.fd < 0)
		return error(_("could not create fd from pipe for '%s'"),
			     server_thread_data->server_data->buf_path.buf);

	ret = read_packetized_to_strbuf(
		reply_data.fd, &buf,
		PACKET_READ_GENTLE_ON_EOF | PACKET_READ_GENTLE_ON_READ_ERROR);
	if (ret >= 0) {
		ret = server_thread_data->server_data->application_cb(
			server_thread_data->server_data->application_data,
			buf.buf, buf.len, do_io_reply_callback, &reply_data);

		packet_flush_gently(reply_data.fd);

		FlushFileBuffers((HANDLE)_get_osfhandle((reply_data.fd)));
	}
	else {
		/*
		 * The client probably disconnected/shutdown before it
		 * could send a well-formed message.  Ignore it.
		 */
	}

	strbuf_release(&buf);
	close(reply_data.fd);

	return ret;
}

/*
 * Handle IPC request and response with this connected client.  And reset
 * the pipe to prepare for the next client.
 */
static int use_connection(struct ipc_server_thread_data *server_thread_data)
{
	int ret;

	ret = do_io(server_thread_data);

	FlushFileBuffers(server_thread_data->hPipe);
	DisconnectNamedPipe(server_thread_data->hPipe);

	return ret;
}

/*
 * Thread proc for an IPC server worker thread.  It handles a series of
 * connections from clients.  It cleans and reuses the hPipe between each
 * client.
 */
static void *server_thread_proc(void *_server_thread_data)
{
	struct ipc_server_thread_data *server_thread_data = _server_thread_data;
	HANDLE hEventConnected = INVALID_HANDLE_VALUE;
	OVERLAPPED oConnect;
	enum connect_result cr;
	int ret;

	assert(server_thread_data->hPipe != INVALID_HANDLE_VALUE);

	trace2_thread_start("ipc-server");
	trace2_data_string("ipc-server", NULL, "pipe",
			   server_thread_data->server_data->buf_path.buf);

	hEventConnected = CreateEventW(NULL, TRUE, FALSE, NULL);

	memset(&oConnect, 0, sizeof(oConnect));
	oConnect.hEvent = hEventConnected;

	for (;;) {
		cr = wait_for_connection(server_thread_data, &oConnect);

		switch (cr) {
		case CR_SHUTDOWN:
			goto finished;

		case CR_CONNECTED:
			ret = use_connection(server_thread_data);
			if (ret == SIMPLE_IPC_QUIT) {
				ipc_server_stop_async(
					server_thread_data->server_data);
				goto finished;
			}
			if (ret > 0) {
				/*
				 * Ignore (transient) IO errors with this
				 * client and reset for the next client.
				 */
			}
			break;

		case CR_CONNECT_PENDING:
			/* By construction, this should not happen. */
			BUG("ipc-server[%s]: unexpeced CR_CONNECT_PENDING",
			    server_thread_data->server_data->buf_path.buf);

		case CR_CONNECT_ERROR:
		case CR_WAIT_ERROR:
			/*
			 * Ignore these theoretical errors.
			 */
			DisconnectNamedPipe(server_thread_data->hPipe);
			break;

		default:
			BUG("unandled case after wait_for_connection");
		}
	}

finished:
	CloseHandle(server_thread_data->hPipe);
	CloseHandle(hEventConnected);

	trace2_thread_exit();
	return NULL;
}

/*
 * We need to build a Windows "SECURITY_ATTRIBUTES" object and use it
 * to apply an ACL when we create the initial instance of the Named
 * Pipe.  The construction is somewhat involved and consists of
 * several sequential steps and intermediate objects.
 *
 * We use this structure to hold these intermediate pointers so that
 * we can free them as a group.  (It is unclear from the docs whether
 * some of these intermediate pointers can be freed before we are
 * finished using the "lpSA" member.)
 */
struct my_sa_data
{
	PSID pEveryoneSID;
	PACL pACL;
	PSECURITY_DESCRIPTOR pSD;
	LPSECURITY_ATTRIBUTES lpSA;
};

static void init_sa(struct my_sa_data *d)
{
	memset(d, 0, sizeof(*d));
}

static void release_sa(struct my_sa_data *d)
{
	if (d->pEveryoneSID)
		FreeSid(d->pEveryoneSID);
	if (d->pACL)
		LocalFree(d->pACL);
	if (d->pSD)
		LocalFree(d->pSD);
	if (d->lpSA)
		LocalFree(d->lpSA);

	memset(d, 0, sizeof(*d));
}

/*
 * Create SECURITY_ATTRIBUTES to apply to the initial named pipe.  The
 * creator of the first server instance gets to set the ACLs on it.
 *
 * We allow the well-known group `EVERYONE` to have read+write access
 * to the named pipe so that clients can send queries to the daemon
 * and receive the response.
 *
 * Normally, this is not necessary since the daemon is usually
 * automatically started by a foreground command like `git status`,
 * but in those cases where an elevated Git command started the daemon
 * (such that the daemon itself runs with elevation), we need to add
 * the ACL so that non-elevated commands can write to it.
 *
 * The following document was helpful:
 * https://docs.microsoft.com/en-us/windows/win32/secauthz/creating-a-security-descriptor-for-a-new-object-in-c--
 *
 * Returns d->lpSA set to a SA or NULL.
 */
static LPSECURITY_ATTRIBUTES get_sa(struct my_sa_data *d)
{
	SID_IDENTIFIER_AUTHORITY sid_auth_world = SECURITY_WORLD_SID_AUTHORITY;
#define NR_EA (1)
	EXPLICIT_ACCESS ea[NR_EA];
	DWORD dwResult;

	if (!AllocateAndInitializeSid(&sid_auth_world, 1,
				      SECURITY_WORLD_RID, 0,0,0,0,0,0,0,
				      &d->pEveryoneSID)) {
		DWORD gle = GetLastError();
		trace2_data_intmax("ipc-debug", NULL, "alloc-world-sid/gle",
				   (intmax_t)gle);
		goto fail;
	}

	memset(ea, 0, NR_EA * sizeof(EXPLICIT_ACCESS));

	ea[0].grfAccessPermissions = GENERIC_READ | GENERIC_WRITE;
	ea[0].grfAccessMode = SET_ACCESS;
	ea[0].grfInheritance = NO_INHERITANCE;
	ea[0].Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
	ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
	ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
	ea[0].Trustee.ptstrName = (LPTSTR)d->pEveryoneSID;

	dwResult = SetEntriesInAcl(NR_EA, ea, NULL, &d->pACL);
	if (dwResult != ERROR_SUCCESS) {
		DWORD gle = GetLastError();
		trace2_data_intmax("ipc-debug", NULL, "set-acl-entry/gle",
				   (intmax_t)gle);
		trace2_data_intmax("ipc-debug", NULL, "set-acl-entry/dw",
				   (intmax_t)dwResult);
		goto fail;
	}

	d->pSD = (PSECURITY_DESCRIPTOR)LocalAlloc(
		LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	if (!InitializeSecurityDescriptor(d->pSD, SECURITY_DESCRIPTOR_REVISION)) {
		DWORD gle = GetLastError();
		trace2_data_intmax("ipc-debug", NULL, "init-sd/gle", (intmax_t)gle);
		goto fail;
	}

	if (!SetSecurityDescriptorDacl(d->pSD, TRUE, d->pACL, FALSE)) {
		DWORD gle = GetLastError();
		trace2_data_intmax("ipc-debug", NULL, "set-sd-dacl/gle", (intmax_t)gle);
		goto fail;
	}

	d->lpSA = (LPSECURITY_ATTRIBUTES)LocalAlloc(LPTR, sizeof(SECURITY_ATTRIBUTES));
	d->lpSA->nLength = sizeof(SECURITY_ATTRIBUTES);
	d->lpSA->lpSecurityDescriptor = d->pSD;
	d->lpSA->bInheritHandle = FALSE;

	return d->lpSA;

fail:
	release_sa(d);
	return NULL;
}

static HANDLE create_new_pipe(wchar_t *wpath, int is_first)
{
	HANDLE hPipe;
	DWORD dwOpenMode, dwPipeMode;
	struct my_sa_data my_sa_data;

	init_sa(&my_sa_data);

	dwOpenMode = PIPE_ACCESS_INBOUND | PIPE_ACCESS_OUTBOUND |
		FILE_FLAG_OVERLAPPED;

	dwPipeMode = PIPE_TYPE_MESSAGE | PIPE_READMODE_BYTE | PIPE_WAIT |
		PIPE_REJECT_REMOTE_CLIENTS;

	if (is_first) {
		dwOpenMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;

		/*
		 * On Windows, the first server pipe instance gets to
		 * set the ACL / Security Attributes on the named
		 * pipe; subsequent instances inherit and cannot
		 * change them.
		 */
		get_sa(&my_sa_data);
	}

	hPipe = CreateNamedPipeW(wpath, dwOpenMode, dwPipeMode,
				 PIPE_UNLIMITED_INSTANCES, 1024, 1024, 0,
				 my_sa_data.lpSA);

	release_sa(&my_sa_data);

	return hPipe;
}

int ipc_server_run_async(struct ipc_server_data **returned_server_data,
			 const char *path, const struct ipc_server_opts *opts,
			 ipc_server_application_cb *application_cb,
			 void *application_data)
{
	struct ipc_server_data *server_data;
	wchar_t wpath[MAX_PATH];
	HANDLE hPipeFirst = INVALID_HANDLE_VALUE;
	int k;
	int ret = 0;
	int nr_threads = opts->nr_threads;

	*returned_server_data = NULL;

	ret = initialize_pipe_name(path, wpath, ARRAY_SIZE(wpath));
	if (ret < 0) {
		errno = EINVAL;
		return -1;
	}

	hPipeFirst = create_new_pipe(wpath, 1);
	if (hPipeFirst == INVALID_HANDLE_VALUE) {
		errno = EADDRINUSE;
		return -2;
	}

	server_data = xcalloc(1, sizeof(*server_data));
	server_data->magic = MAGIC_SERVER_DATA;
	server_data->application_cb = application_cb;
	server_data->application_data = application_data;
	server_data->hEventStopRequested = CreateEvent(NULL, TRUE, FALSE, NULL);
	strbuf_init(&server_data->buf_path, 0);
	strbuf_addstr(&server_data->buf_path, path);
	wcscpy(server_data->wpath, wpath);

	if (nr_threads < 1)
		nr_threads = 1;

	for (k = 0; k < nr_threads; k++) {
		struct ipc_server_thread_data *std;

		std = xcalloc(1, sizeof(*std));
		std->magic = MAGIC_SERVER_THREAD_DATA;
		std->server_data = server_data;
		std->hPipe = INVALID_HANDLE_VALUE;

		std->hPipe = (k == 0)
			? hPipeFirst
			: create_new_pipe(server_data->wpath, 0);

		if (std->hPipe == INVALID_HANDLE_VALUE) {
			/*
			 * If we've reached a pipe instance limit for
			 * this path, just use fewer threads.
			 */
			free(std);
			break;
		}

		if (pthread_create(&std->pthread_id, NULL,
				   server_thread_proc, std)) {
			/*
			 * Likewise, if we're out of threads, just use
			 * fewer threads than requested.
			 *
			 * However, we just give up if we can't even get
			 * one thread.  This should not happen.
			 */
			if (k == 0)
				die(_("could not start thread[0] for '%s'"),
				    path);

			CloseHandle(std->hPipe);
			free(std);
			break;
		}

		std->next_thread = server_data->thread_list;
		server_data->thread_list = std;
	}

	*returned_server_data = server_data;
	return 0;
}

int ipc_server_stop_async(struct ipc_server_data *server_data)
{
	if (!server_data)
		return 0;

	/*
	 * Gently tell all of the ipc_server threads to shutdown.
	 * This will be seen the next time they are idle (and waiting
	 * for a connection).
	 *
	 * We DO NOT attempt to force them to drop an active connection.
	 */
	SetEvent(server_data->hEventStopRequested);
	return 0;
}

int ipc_server_await(struct ipc_server_data *server_data)
{
	DWORD dwWaitResult;

	if (!server_data)
		return 0;

	dwWaitResult = WaitForSingleObject(server_data->hEventStopRequested, INFINITE);
	if (dwWaitResult != WAIT_OBJECT_0)
		return error(_("wait for hEvent failed for '%s'"),
			     server_data->buf_path.buf);

	while (server_data->thread_list) {
		struct ipc_server_thread_data *std = server_data->thread_list;

		pthread_join(std->pthread_id, NULL);

		server_data->thread_list = std->next_thread;
		free(std);
	}

	server_data->is_stopped = 1;

	return 0;
}

void ipc_server_free(struct ipc_server_data *server_data)
{
	if (!server_data)
		return;

	if (!server_data->is_stopped)
		BUG("cannot free ipc-server while running for '%s'",
		    server_data->buf_path.buf);

	strbuf_release(&server_data->buf_path);

	if (server_data->hEventStopRequested != INVALID_HANDLE_VALUE)
		CloseHandle(server_data->hEventStopRequested);

	while (server_data->thread_list) {
		struct ipc_server_thread_data *std = server_data->thread_list;

		server_data->thread_list = std->next_thread;
		free(std);
	}

	free(server_data);
}
