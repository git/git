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

static interval_fn has_worktree_moved;

static interval_fn *table[] = {
	has_worktree_moved,
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

	struct wt_moved
	{
		wchar_t wpath[MAX_LONG_PATH + 1];
		BY_HANDLE_FILE_INFORMATION bhfi;
	} wt_moved;
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

static int lookup_bhfi(wchar_t *wpath,
		       BY_HANDLE_FILE_INFORMATION *bhfi)
{
	DWORD desired_access = FILE_LIST_DIRECTORY;
	DWORD share_mode =
		FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE;
	HANDLE hDir;

	hDir = CreateFileW(wpath, desired_access, share_mode, NULL,
			   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (hDir == INVALID_HANDLE_VALUE) {
		error(_("[GLE %ld] health thread could not open '%ls'"),
		      GetLastError(), wpath);
		return -1;
	}

	if (!GetFileInformationByHandle(hDir, bhfi)) {
		error(_("[GLE %ld] health thread getting BHFI for '%ls'"),
		      GetLastError(), wpath);
		CloseHandle(hDir);
		return -1;
	}

	CloseHandle(hDir);
	return 0;
}

static int bhfi_eq(const BY_HANDLE_FILE_INFORMATION *bhfi_1,
		   const BY_HANDLE_FILE_INFORMATION *bhfi_2)
{
	return (bhfi_1->dwVolumeSerialNumber == bhfi_2->dwVolumeSerialNumber &&
		bhfi_1->nFileIndexHigh == bhfi_2->nFileIndexHigh &&
		bhfi_1->nFileIndexLow == bhfi_2->nFileIndexLow);
}

/*
 * Shutdown if the original worktree root directory been deleted,
 * moved, or renamed?
 *
 * Since the main thread did a "chdir(getenv($HOME))" and our CWD
 * is not in the worktree root directory and because the listener
 * thread added FILE_SHARE_DELETE to the watch handle, it is possible
 * for the root directory to be moved or deleted while we are still
 * watching it.  We want to detect that here and force a shutdown.
 *
 * Granted, a delete MAY cause some operations to fail, such as
 * GetOverlappedResult(), but it is not guaranteed.  And because
 * ReadDirectoryChangesW() only reports on changes *WITHIN* the
 * directory, not changes *ON* the directory, our watch will not
 * receive a delete event for it.
 *
 * A move/rename of the worktree root will also not generate an event.
 * And since the listener thread already has an open handle, it may
 * continue to receive events for events within the directory.
 * However, the pathname of the named-pipe was constructed using the
 * original location of the worktree root.  (Remember named-pipes are
 * stored in the NPFS and not in the actual file system.)  Clients
 * trying to talk to the worktree after the move/rename will not
 * reach our daemon process, since we're still listening on the
 * pipe with original path.
 *
 * Furthermore, if the user does something like:
 *
 *   $ mv repo repo.old
 *   $ git init repo
 *
 * A new daemon cannot be started in the new instance of "repo"
 * because the named-pipe is still being used by the daemon on
 * the original instance.
 *
 * So, detect move/rename/delete and shutdown.  This should also
 * handle unsafe drive removal.
 *
 * We use the file system unique ID to distinguish the original
 * directory instance from a new instance and force a shutdown
 * if the unique ID changes.
 *
 * Since a worktree move/rename/delete/unmount doesn't happen
 * that often (and we can't get an immediate event anyway), we
 * use a timeout and periodically poll it.
 */
static int has_worktree_moved(struct fsmonitor_daemon_state *state,
			      enum interval_fn_ctx ctx)
{
	struct fsm_health_data *data = state->health_data;
	BY_HANDLE_FILE_INFORMATION bhfi;
	int r;

	switch (ctx) {
	case CTX_TERM:
		return 0;

	case CTX_INIT:
		if (xutftowcs_long_path(data->wt_moved.wpath,
					state->path_worktree_watch.buf) < 0) {
			error(_("could not convert to wide characters: '%s'"),
			      state->path_worktree_watch.buf);
			return -1;
		}

		/*
		 * On the first call we lookup the unique sequence ID for
		 * the worktree root directory.
		 */
		return lookup_bhfi(data->wt_moved.wpath, &data->wt_moved.bhfi);

	case CTX_TIMER:
		r = lookup_bhfi(data->wt_moved.wpath, &bhfi);
		if (r)
			return r;
		if (!bhfi_eq(&data->wt_moved.bhfi, &bhfi)) {
			error(_("BHFI changed '%ls'"), data->wt_moved.wpath);
			return -1;
		}
		return 0;

	default:
		die("unhandled case in 'has_worktree_moved': %d",
		    (int)ctx);
	}

	return 0;
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
