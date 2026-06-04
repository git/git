#ifndef FSMONITOR_IPC_H
#define FSMONITOR_IPC_H

#include "simple-ipc.h"

struct repository;

/*
 * Returns true if built-in file system monitor daemon is defined
 * for this platform.
 */
int fsmonitor_ipc__is_supported(void);

/*
 * Returns the pathname to the IPC named pipe or Unix domain socket
 * where a `git-fsmonitor--daemon` process will listen.  This is a
 * per-worktree value.
 *
 * Returns NULL if the daemon is not supported on this platform.
 */
const char *fsmonitor_ipc__get_path(struct repository *r);

/*
 * Try to determine whether there is a `git-fsmonitor--daemon` process
 * listening on the IPC pipe/socket.
 */
enum ipc_active_state fsmonitor_ipc__get_state(void);

/*
 * Connect to a `git-fsmonitor--daemon` process via simple-ipc
 * and ask for the set of changed files since the given token.
 *
 * Spawn a daemon process in the background if necessary.
 *
 * Returns -1 on error; 0 on success.
 */
int fsmonitor_ipc__send_query(const char *since_token,
			      struct strbuf *answer);

/*
 * Connect to a `git-fsmonitor--daemon` process via simple-ipc and
 * send a command verb.  If no daemon is available, we DO NOT try to
 * start one.
 *
 * Returns -1 on error; 0 on success.
 */
int fsmonitor_ipc__send_command(const char *command,
				struct strbuf *answer);

#endif /* FSMONITOR_IPC_H */
