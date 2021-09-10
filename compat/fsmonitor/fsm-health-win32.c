#include "cache.h"
#include "config.h"
#include "fsmonitor.h"
#include "fsm-health.h"
#include "fsmonitor--daemon.h"

/*
 * Every minute wake up and test our health.
 */
#define WAIT_FREQ_MS (60 * 1000)

enum interval_fn_ctx { CTX_INIT = 0, CTX_TERM, CTX_TIMER };

typedef int (interval_fn)(struct fsmonitor_daemon_state *state,
			  enum interval_fn_ctx ctx);

static interval_fn *table[] = {
	NULL, /* must be last */
};

/*
 * Call all of the functions in the table.
 * Shortcut and return first error.
 *
 * Return 0 if all succeeded.
 */
static int call_all(struct fsmonitor_daemon_state *state,
		    enum interval_fn_ctx ctx)
{
	int k;

	for (k = 0; table[k]; k++) {
		int r = table[k](state, ctx);
		if (r)
			return r;
	}

	return 0;
}

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
	int r;

	r = call_all(state, CTX_INIT);
	if (r < 0)
		goto force_error_stop;
	if (r > 0)
		goto force_shutdown;

	for (;;) {
		DWORD dwWait = WaitForMultipleObjects(data->nr_handles,
						      data->hHandles,
						      FALSE, WAIT_FREQ_MS);

		if (dwWait == WAIT_OBJECT_0 + HEALTH_SHUTDOWN)
			goto clean_shutdown;

		if (dwWait == WAIT_TIMEOUT) {
			r = call_all(state, CTX_TIMER);
			if (r < 0)
				goto force_error_stop;
			if (r > 0)
				goto force_shutdown;
			continue;
		}

		error(_("health thread wait failed [GLE %ld]"),
		      GetLastError());
		goto force_error_stop;
	}

force_error_stop:
	state->health_error_code = -1;
force_shutdown:
	ipc_server_stop_async(state->ipc_server_data);
clean_shutdown:
	call_all(state, CTX_TERM);
	return;
}

void fsm_health__stop_async(struct fsmonitor_daemon_state *state)
{
	SetEvent(state->health_data->hHandles[HEALTH_SHUTDOWN]);
}
