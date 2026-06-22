#ifndef FSM_HEALTH_H
#define FSM_HEALTH_H

/* This needs to be implemented by each backend */

#ifdef HAVE_FSMONITOR_DAEMON_BACKEND

struct fsmonitor_daemon_state;

/*
 * Initialize platform-specific data for the fsmonitor health thread.
 * This will be called from the main thread PRIOR to staring the
 * thread.
 *
 * Returns 0 if successful.
 * Returns -1 otherwise.
 */
int fsm_health__ctor(struct fsmonitor_daemon_state *state);

/*
 * Cleanup platform-specific data for the health thread.
 * This will be called from the main thread AFTER joining the thread.
 */
void fsm_health__dtor(struct fsmonitor_daemon_state *state);

/*
 * The main body of the platform-specific event loop to monitor the
 * health of the daemon process.  This will run in the health thread.
 *
 * The health thread should call `ipc_server_stop_async()` if it needs
 * to cause a shutdown.  (It should NOT do so if it receives a shutdown
 * shutdown signal.)
 *
 * It should set `state->health_error_code` to -1 if the daemon should exit
 * with an error.
 */
void fsm_health__loop(struct fsmonitor_daemon_state *state);

/*
 * Gently request that the health thread shutdown.
 * It does not wait for it to stop.  The caller should do a JOIN
 * to wait for it.
 */
void fsm_health__stop_async(struct fsmonitor_daemon_state *state);

#endif /* HAVE_FSMONITOR_DAEMON_BACKEND */
#endif /* FSM_HEALTH_H */
