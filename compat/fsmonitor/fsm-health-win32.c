#include "cache.h"
#include "config.h"
#include "fsmonitor.h"
#include "fsm-health.h"
#include "fsmonitor--daemon.h"

struct fsm_health_data
{
	HANDLE hEventShutdown;

	HANDLE hHandles[1]; /* the array does not own these handles */
#define HEALTH_SHUTDOWN 0
	int nr_handles; /* number of active event handles */
};

int fsm_health__ctor(struct fsmonitor_daemon_state *state)
{
	struct fsm_health_data *data;

	CALLOC_ARRAY(data, 1);

	data->hEventShutdown = CreateEvent(NULL, TRUE, FALSE, NULL);

	data->hHandles[HEALTH_SHUTDOWN] = data->hEventShutdown;
	data->nr_handles++;

	state->health_data = data;
	return 0;
}

void fsm_health__dtor(struct fsmonitor_daemon_state *state)
{
	struct fsm_health_data *data;

	if (!state || !state->health_data)
		return;

	data = state->health_data;

	CloseHandle(data->hEventShutdown);

	FREE_AND_NULL(state->health_data);
}

void fsm_health__loop(struct fsmonitor_daemon_state *state)
{
	struct fsm_health_data *data = state->health_data;

	for (;;) {
		DWORD dwWait = WaitForMultipleObjects(data->nr_handles,
						      data->hHandles,
						      FALSE, INFINITE);

		if (dwWait == WAIT_OBJECT_0 + HEALTH_SHUTDOWN)
			goto clean_shutdown;

		error(_("health thread wait failed [GLE %ld]"),
		      GetLastError());
		goto force_error_stop;
	}

force_error_stop:
	state->health_error_code = -1;
	ipc_server_stop_async(state->ipc_server_data);
clean_shutdown:
	return;
}

void fsm_health__stop_async(struct fsmonitor_daemon_state *state)
{
	SetEvent(state->health_data->hHandles[HEALTH_SHUTDOWN]);
}
