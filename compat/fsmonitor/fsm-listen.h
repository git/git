#ifndef FSM_LISTEN_H
#define FSM_LISTEN_H

/* This needs to be implemented by each backend */

#ifdef HAVE_FSMONITOR_DAEMON_BACKEND

struct fsmonitor_daemon_state;

/*
 * Initialize platform-specific data for the fsmonitor listener thread.
 * This will be called from the main thread PRIOR to staring the
 * fsmonitor_fs_listener thread.
 *
 * Returns 0 if successful.
 * Returns -1 otherwise.
 */
int fsm_listen__ctor(struct fsmonitor_daemon_state *state);

/*
 * Cleanup platform-specific data for the fsmonitor listener thread.
 * This will be called from the main thread AFTER joining the listener.
 */
void fsm_listen__dtor(struct fsmonitor_daemon_state *state);

/*
 * The main body of the platform-specific event loop to watch for
 * filesystem events.  This will run in the fsmonitor_fs_listen thread.
 *
 * It should call `ipc_server_stop_async()` if the listener thread
 * prematurely terminates (because of a filesystem error or if it
 * detects that the .git directory has been deleted).  (It should NOT
 * do so if the listener thread receives a normal shutdown signal from
 * the IPC layer.)
 *
 * It should set `state->error_code` to -1 if the daemon should exit
 * with an error.
 */
void fsm_listen__loop(struct fsmonitor_daemon_state *state);

/*
 * Gently request that the fsmonitor listener thread shutdown.
 * It does not wait for it to stop.  The caller should do a JOIN
 * to wait for it.
 */
void fsm_listen__stop_async(struct fsmonitor_daemon_state *state);

#endif /* HAVE_FSMONITOR_DAEMON_BACKEND */
#endif /* FSM_LISTEN_H */
