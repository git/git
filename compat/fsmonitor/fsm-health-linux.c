#include "git-compat-util.h"
#include "config.h"
#include "fsmonitor-ll.h"
#include "fsm-health.h"
#include "fsmonitor--daemon.h"

/*
 * The Linux fsmonitor implementation uses inotify which has its own
 * mechanisms for detecting filesystem unmount and other events that
 * would require the daemon to shutdown.  Therefore, we don't need
 * a separate health thread like Windows does.
 *
 * These stub functions satisfy the interface requirements.
 */

int fsm_health__ctor(struct fsmonitor_daemon_state *state UNUSED)
{
	return 0;
}

void fsm_health__dtor(struct fsmonitor_daemon_state *state UNUSED)
{
	return;
}

void fsm_health__loop(struct fsmonitor_daemon_state *state UNUSED)
{
	return;
}

void fsm_health__stop_async(struct fsmonitor_daemon_state *state UNUSED)
{
}
