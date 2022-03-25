#include "cache.h"
#include "config.h"
#include "fsmonitor.h"
#include "fsm-listen.h"
#include "fsmonitor--daemon.h"
#include "gettext.h"
#include "trace2.h"

/*
 * The documentation of ReadDirectoryChangesW() states that the maximum
 * buffer size is 64K when the monitored directory is remote.
 *
 * Larger buffers may be used when the monitored directory is local and
 * will help us receive events faster from the kernel and avoid dropped
 * events.
 *
 * So we try to use a very large buffer and silently fallback to 64K if
 * we get an error.
 */
#define MAX_RDCW_BUF_FALLBACK (65536)
#define MAX_RDCW_BUF          (65536 * 8)

struct one_watch
{
	char buffer[MAX_RDCW_BUF];
	DWORD buf_len;
	DWORD count;

	struct strbuf path;
	wchar_t wpath_longname[MAX_LONG_PATH + 1];
	DWORD wpath_longname_len;

	HANDLE hDir;
	HANDLE hEvent;
	OVERLAPPED overlapped;

	/*
	 * Is there an active ReadDirectoryChangesW() call pending.  If so, we
	 * need to later call GetOverlappedResult() and possibly CancelIoEx().
	 */
	BOOL is_active;

	/*
	 * Are shortnames enabled on the containing drive?  This is
	 * always true for "C:/" drives and usually never true for
	 * other drives.
	 *
	 * We only set this for the worktree because we only need to
	 * convert shortname paths to longname paths for items we send
	 * to clients.  (We don't care about shortname expansion for
	 * paths inside a GITDIR because we never send them to
	 * clients.)
	 */
	BOOL has_shortnames;
	BOOL has_tilde;
	wchar_t dotgit_shortname[16]; /* for 8.3 name */
};

struct fsm_listen_data
{
	struct one_watch *watch_worktree;
	struct one_watch *watch_gitdir;

	HANDLE hEventShutdown;

	HANDLE hListener[3]; /* we don't own these handles */
#define LISTENER_SHUTDOWN 0
#define LISTENER_HAVE_DATA_WORKTREE 1
#define LISTENER_HAVE_DATA_GITDIR 2
	int nr_listener_handles;
};

/*
 * Convert the WCHAR path from the event into UTF8 and normalize it.
 *
 * `wpath_len` is in WCHARS not bytes.
 */
static int normalize_path_in_utf8(wchar_t *wpath, DWORD wpath_len,
				  struct strbuf *normalized_path)
{
	int reserve;
	int len = 0;

	strbuf_reset(normalized_path);
	if (!wpath_len)
		goto normalize;

	/*
	 * Pre-reserve enough space in the UTF8 buffer for
	 * each Unicode WCHAR character to be mapped into a
	 * sequence of 2 UTF8 characters.  That should let us
	 * avoid ERROR_INSUFFICIENT_BUFFER 99.9+% of the time.
	 */
	reserve = 2 * wpath_len + 1;
	strbuf_grow(normalized_path, reserve);

	for (;;) {
		len = WideCharToMultiByte(CP_UTF8, 0,
					  wpath, wpath_len,
					  normalized_path->buf,
					  strbuf_avail(normalized_path) - 1,
					  NULL, NULL);
		if (len > 0)
			goto normalize;
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			error(_("[GLE %ld] could not convert path to UTF-8: '%.*ls'"),
			      GetLastError(), (int)wpath_len, wpath);
			return -1;
		}

		strbuf_grow(normalized_path,
			    strbuf_avail(normalized_path) + reserve);
	}

normalize:
	strbuf_setlen(normalized_path, len);
	return strbuf_normalize_path(normalized_path);
}

/*
 * See if the worktree root directory has shortnames enabled.
 * This will help us decide if we need to do an expensive shortname
 * to longname conversion on every notification event.
 *
 * We do not want to create a file to test this, so we assume that the
 * root directory contains a ".git" file or directory.  (Our caller
 * only calls us for the worktree root, so this should be fine.)
 *
 * Remember the spelling of the shortname for ".git" if it exists.
 */
static void check_for_shortnames(struct one_watch *watch)
{
	wchar_t buf_in[MAX_LONG_PATH + 1];
	wchar_t buf_out[MAX_LONG_PATH + 1];
	wchar_t *last;
	wchar_t *p;

	/* build L"<wt-root-path>/.git" */
	swprintf(buf_in, ARRAY_SIZE(buf_in) - 1, L"%ls.git",
		 watch->wpath_longname);

	if (!GetShortPathNameW(buf_in, buf_out, ARRAY_SIZE(buf_out)))
		return;

	/*
	 * Get the final filename component of the shortpath.
	 * We know that the path does not have a final slash.
	 */
	for (last = p = buf_out; *p; p++)
		if (*p == L'/' || *p == '\\')
			last = p + 1;

	if (!wcscmp(last, L".git"))
		return;

	watch->has_shortnames = 1;
	wcsncpy(watch->dotgit_shortname, last,
		ARRAY_SIZE(watch->dotgit_shortname));

	/*
	 * The shortname for ".git" is usually of the form "GIT~1", so
	 * we should be able to avoid shortname to longname mapping on
	 * every notification event if the source string does not
	 * contain a "~".
	 *
	 * However, the documentation for GetLongPathNameW() says
	 * that there are filesystems that don't follow that pattern
	 * and warns against this optimization.
	 *
	 * Lets test this.
	 */
	if (wcschr(watch->dotgit_shortname, L'~'))
		watch->has_tilde = 1;
}

enum get_relative_result {
	GRR_NO_CONVERSION_NEEDED,
	GRR_HAVE_CONVERSION,
	GRR_SHUTDOWN,
};

/*
 * Info notification paths are relative to the root of the watch.
 * If our CWD is still at the root, then we can use relative paths
 * to convert from shortnames to longnames.  If our process has a
 * different CWD, then we need to construct an absolute path, do
 * the conversion, and then return the root-relative portion.
 *
 * We use the longname form of the root as our basis and assume that
 * it already has a trailing slash.
 *
 * `wpath_len` is in WCHARS not bytes.
 */
static enum get_relative_result get_relative_longname(
	struct one_watch *watch,
	const wchar_t *wpath, DWORD wpath_len,
	wchar_t *wpath_longname, size_t bufsize_wpath_longname)
{
	wchar_t buf_in[2 * MAX_LONG_PATH + 1];
	wchar_t buf_out[MAX_LONG_PATH + 1];
	DWORD root_len;
	DWORD out_len;

	/*
	 * Build L"<wt-root-path>/<event-rel-path>"
	 * Note that the <event-rel-path> might not be null terminated
	 * so we avoid swprintf() constructions.
	 */
	root_len = watch->wpath_longname_len;
	if (root_len + wpath_len >= ARRAY_SIZE(buf_in)) {
		/*
		 * This should not happen.  We cannot append the observed
		 * relative path onto the end of the worktree root path
		 * without overflowing the buffer.  Just give up.
		 */
		return GRR_SHUTDOWN;
	}
	wcsncpy(buf_in, watch->wpath_longname, root_len);
	wcsncpy(buf_in + root_len, wpath, wpath_len);
	buf_in[root_len + wpath_len] = 0;

	/*
	 * We don't actually know if the source pathname is a
	 * shortname or a longname.  This Windows routine allows
	 * either to be given as input.
	 */
	out_len = GetLongPathNameW(buf_in, buf_out, ARRAY_SIZE(buf_out));
	if (!out_len) {
		/*
		 * The shortname to longname conversion can fail for
		 * various reasons, for example if the file has been
		 * deleted.  (That is, if we just received a
		 * delete-file notification event and the file is
		 * already gone, we can't ask the file system to
		 * lookup the longname for it.  Likewise, for moves
		 * and renames where we are given the old name.)
		 *
		 * Since deleting or moving a file or directory by its
		 * shortname is rather obscure, I'm going ignore the
		 * failure and ask the caller to report the original
		 * relative path.  This seems kinder than failing here
		 * and forcing a resync.  Besides, forcing a resync on
		 * every file/directory delete would effectively
		 * cripple monitoring.
		 *
		 * We might revisit this in the future.
		 */
		return GRR_NO_CONVERSION_NEEDED;
	}

	if (!wcscmp(buf_in, buf_out)) {
		/*
		 * The path does not have a shortname alias.
		 */
		return GRR_NO_CONVERSION_NEEDED;
	}

	if (wcsncmp(buf_in, buf_out, root_len)) {
		/*
		 * The spelling of the root directory portion of the computed
		 * longname has changed.  This should not happen.  Basically,
		 * it means that we don't know where (without recomputing the
		 * longname of just the root directory) to split out the
		 * relative path.  Since this should not happen, I'm just
		 * going to let this fail and force a shutdown (because all
		 * subsequent events are probably going to see the same
		 * mismatch).
		 */
		return GRR_SHUTDOWN;
	}

	if (out_len - root_len >= bufsize_wpath_longname) {
		/*
		 * This should not happen.  We cannot copy the root-relative
		 * portion of the path into the provided buffer without an
		 * overrun.  Just give up.
		 */
		return GRR_SHUTDOWN;
	}

	/* Return the worktree root-relative portion of the longname. */

	wcscpy(wpath_longname, buf_out + root_len);
	return GRR_HAVE_CONVERSION;
}

void fsm_listen__stop_async(struct fsmonitor_daemon_state *state)
{
	SetEvent(state->listen_data->hListener[LISTENER_SHUTDOWN]);
}

static struct one_watch *create_watch(struct fsmonitor_daemon_state *state,
				      const char *path)
{
	struct one_watch *watch = NULL;
	DWORD desired_access = FILE_LIST_DIRECTORY;
	DWORD share_mode =
		FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE;
	HANDLE hDir;
	DWORD len_longname;
	wchar_t wpath[MAX_LONG_PATH + 1];
	wchar_t wpath_longname[MAX_LONG_PATH + 1];

	if (xutftowcs_long_path(wpath, path) < 0) {
		error(_("could not convert to wide characters: '%s'"), path);
		return NULL;
	}

	hDir = CreateFileW(wpath,
			   desired_access, share_mode, NULL, OPEN_EXISTING,
			   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
			   NULL);
	if (hDir == INVALID_HANDLE_VALUE) {
		error(_("[GLE %ld] could not watch '%s'"),
		      GetLastError(), path);
		return NULL;
	}

	len_longname = GetLongPathNameW(wpath, wpath_longname,
					ARRAY_SIZE(wpath_longname));
	if (!len_longname) {
		error(_("[GLE %ld] could not get longname of '%s'"),
		      GetLastError(), path);
		CloseHandle(hDir);
		return NULL;
	}

	if (wpath_longname[len_longname - 1] != L'/' &&
	    wpath_longname[len_longname - 1] != L'\\') {
		wpath_longname[len_longname++] = L'/';
		wpath_longname[len_longname] = 0;
	}

	CALLOC_ARRAY(watch, 1);

	watch->buf_len = sizeof(watch->buffer); /* assume full MAX_RDCW_BUF */

	strbuf_init(&watch->path, 0);
	strbuf_addstr(&watch->path, path);

	wcscpy(watch->wpath_longname, wpath_longname);
	watch->wpath_longname_len = len_longname;

	watch->hDir = hDir;
	watch->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	return watch;
}

static void destroy_watch(struct one_watch *watch)
{
	if (!watch)
		return;

	strbuf_release(&watch->path);
	if (watch->hDir != INVALID_HANDLE_VALUE)
		CloseHandle(watch->hDir);
	if (watch->hEvent != INVALID_HANDLE_VALUE)
		CloseHandle(watch->hEvent);

	free(watch);
}

static int start_rdcw_watch(struct fsm_listen_data *data,
			    struct one_watch *watch)
{
	DWORD dwNotifyFilter =
		FILE_NOTIFY_CHANGE_FILE_NAME |
		FILE_NOTIFY_CHANGE_DIR_NAME |
		FILE_NOTIFY_CHANGE_ATTRIBUTES |
		FILE_NOTIFY_CHANGE_SIZE |
		FILE_NOTIFY_CHANGE_LAST_WRITE |
		FILE_NOTIFY_CHANGE_CREATION;

	ResetEvent(watch->hEvent);

	memset(&watch->overlapped, 0, sizeof(watch->overlapped));
	watch->overlapped.hEvent = watch->hEvent;

	/*
	 * Queue an async call using Overlapped IO.  This returns immediately.
	 * Our event handle will be signalled when the real result is available.
	 *
	 * The return value here just means that we successfully queued it.
	 * We won't know if the Read...() actually produces data until later.
	 */
	watch->is_active = ReadDirectoryChangesW(
		watch->hDir, watch->buffer, watch->buf_len, TRUE,
		dwNotifyFilter, &watch->count, &watch->overlapped, NULL);

	if (watch->is_active)
		return 0;

	error(_("ReadDirectoryChangedW failed on '%s' [GLE %ld]"),
	      watch->path.buf, GetLastError());
	return -1;
}

static int recv_rdcw_watch(struct one_watch *watch)
{
	DWORD gle;

	watch->is_active = FALSE;

	/*
	 * The overlapped result is ready.  If the Read...() was successful
	 * we finally receive the actual result into our buffer.
	 */
	if (GetOverlappedResult(watch->hDir, &watch->overlapped, &watch->count,
				TRUE))
		return 0;

	gle = GetLastError();
	if (gle == ERROR_INVALID_PARAMETER &&
	    /*
	     * The kernel throws an invalid parameter error when our
	     * buffer is too big and we are pointed at a remote
	     * directory (and possibly for other reasons).  Quietly
	     * set it down and try again.
	     *
	     * See note about MAX_RDCW_BUF at the top.
	     */
	    watch->buf_len > MAX_RDCW_BUF_FALLBACK) {
		watch->buf_len = MAX_RDCW_BUF_FALLBACK;
		return -2;
	}

	/*
	 * GetOverlappedResult() fails if the watched directory is
	 * deleted while we were waiting for an overlapped IO to
	 * complete.  The documentation did not list specific errors,
	 * but I observed ERROR_ACCESS_DENIED (0x05) errors during
	 * testing.
	 *
	 * Note that we only get notificaiton events for events
	 * *within* the directory, not *on* the directory itself.
	 * (These might be properies of the parent directory, for
	 * example).
	 *
	 * NEEDSWORK: We might try to check for the deleted directory
	 * case and return a better error message, but I'm not sure it
	 * is worth it.
	 *
	 * Shutdown if we get any error.
	 */

	error(_("GetOverlappedResult failed on '%s' [GLE %ld]"),
	      watch->path.buf, gle);
	return -1;
}

static void cancel_rdcw_watch(struct one_watch *watch)
{
	DWORD count;

	if (!watch || !watch->is_active)
		return;

	/*
	 * The calls to ReadDirectoryChangesW() and GetOverlappedResult()
	 * form a "pair" (my term) where we queue an IO and promise to
	 * hang around and wait for the kernel to give us the result.
	 *
	 * If for some reason after we queue the IO, we have to quit
	 * or otherwise not stick around for the second half, we must
	 * tell the kernel to abort the IO.  This prevents the kernel
	 * from writing to our buffer and/or signalling our event
	 * after we free them.
	 *
	 * (Ask me how much fun it was to track that one down).
	 */
	CancelIoEx(watch->hDir, &watch->overlapped);
	GetOverlappedResult(watch->hDir, &watch->overlapped, &count, TRUE);
	watch->is_active = FALSE;
}

/*
 * Process a single relative pathname event.
 * Return 1 if we should shutdown.
 */
static int process_1_worktree_event(
	struct string_list *cookie_list,
	struct fsmonitor_batch **batch,
	const struct strbuf *path,
	enum fsmonitor_path_type t,
	DWORD info_action)
{
	const char *slash;

	switch (t) {
	case IS_INSIDE_DOT_GIT_WITH_COOKIE_PREFIX:
		/* special case cookie files within .git */

		/* Use just the filename of the cookie file. */
		slash = find_last_dir_sep(path->buf);
		string_list_append(cookie_list,
				   slash ? slash + 1 : path->buf);
		break;

	case IS_INSIDE_DOT_GIT:
		/* ignore everything inside of "<worktree>/.git/" */
		break;

	case IS_DOT_GIT:
		/* "<worktree>/.git" was deleted (or renamed away) */
		if ((info_action == FILE_ACTION_REMOVED) ||
		    (info_action == FILE_ACTION_RENAMED_OLD_NAME)) {
			trace2_data_string("fsmonitor", NULL,
					   "fsm-listen/dotgit",
					   "removed");
			return 1;
		}
		break;

	case IS_WORKDIR_PATH:
		/* queue normal pathname */
		if (!*batch)
			*batch = fsmonitor_batch__new();
		fsmonitor_batch__add_path(*batch, path->buf);
		break;

	case IS_GITDIR:
	case IS_INSIDE_GITDIR:
	case IS_INSIDE_GITDIR_WITH_COOKIE_PREFIX:
	default:
		BUG("unexpected path classification '%d' for '%s'",
		    t, path->buf);
	}

	return 0;
}

/*
 * Process filesystem events that happen anywhere (recursively) under the
 * <worktree> root directory.  For a normal working directory, this includes
 * both version controlled files and the contents of the .git/ directory.
 *
 * If <worktree>/.git is a file, then we only see events for the file
 * itself.
 */
static int process_worktree_events(struct fsmonitor_daemon_state *state)
{
	struct fsm_listen_data *data = state->listen_data;
	struct one_watch *watch = data->watch_worktree;
	struct strbuf path = STRBUF_INIT;
	struct string_list cookie_list = STRING_LIST_INIT_DUP;
	struct fsmonitor_batch *batch = NULL;
	const char *p = watch->buffer;
	wchar_t wpath_longname[MAX_LONG_PATH + 1];

	/*
	 * If the kernel gets more events than will fit in the kernel
	 * buffer associated with our RDCW handle, it drops them and
	 * returns a count of zero.
	 *
	 * Yes, the call returns WITHOUT error and with length zero.
	 * This is the documented behavior.  (My testing has confirmed
	 * that it also sets the last error to ERROR_NOTIFY_ENUM_DIR,
	 * but we do not rely on that since the function did not
	 * return an error and it is not documented.)
	 *
	 * (The "overflow" case is not ambiguous with the "no data" case
	 * because we did an INFINITE wait.)
	 *
	 * This means we have a gap in coverage.  Tell the daemon layer
	 * to resync.
	 */
	if (!watch->count) {
		trace2_data_string("fsmonitor", NULL, "fsm-listen/kernel",
				   "overflow");
		fsmonitor_force_resync(state);
		return LISTENER_HAVE_DATA_WORKTREE;
	}

	/*
	 * On Windows, `info` contains an "array" of paths that are
	 * relative to the root of whichever directory handle received
	 * the event.
	 */
	for (;;) {
		FILE_NOTIFY_INFORMATION *info = (void *)p;
		wchar_t *wpath = info->FileName;
		DWORD wpath_len = info->FileNameLength / sizeof(WCHAR);
		enum fsmonitor_path_type t;
		enum get_relative_result grr;

		if (watch->has_shortnames) {
			if (!wcscmp(wpath, watch->dotgit_shortname)) {
				/*
				 * This event exactly matches the
				 * spelling of the shortname of
				 * ".git", so we can skip some steps.
				 *
				 * (This case is odd because the user
				 * can "rm -rf GIT~1" and we cannot
				 * use the filesystem to map it back
				 * to ".git".)
				 */
				strbuf_reset(&path);
				strbuf_addstr(&path, ".git");
				t = IS_DOT_GIT;
				goto process_it;
			}

			if (watch->has_tilde && !wcschr(wpath, L'~')) {
				/*
				 * Shortnames on this filesystem have tildes
				 * and the notification path does not have
				 * one, so we assume that it is a longname.
				 */
				goto normalize_it;
			}

			grr = get_relative_longname(watch, wpath, wpath_len,
						    wpath_longname,
						    ARRAY_SIZE(wpath_longname));
			switch (grr) {
			case GRR_NO_CONVERSION_NEEDED: /* use info buffer as is */
				break;
			case GRR_HAVE_CONVERSION:
				wpath = wpath_longname;
				wpath_len = wcslen(wpath);
				break;
			default:
			case GRR_SHUTDOWN:
				goto force_shutdown;
			}
		}

normalize_it:
		if (normalize_path_in_utf8(wpath, wpath_len, &path) == -1)
			goto skip_this_path;

		t = fsmonitor_classify_path_workdir_relative(path.buf);

process_it:
		if (process_1_worktree_event(&cookie_list, &batch, &path, t,
					     info->Action))
			goto force_shutdown;

skip_this_path:
		if (!info->NextEntryOffset)
			break;
		p += info->NextEntryOffset;
	}

	fsmonitor_publish(state, batch, &cookie_list);
	batch = NULL;
	string_list_clear(&cookie_list, 0);
	strbuf_release(&path);
	return LISTENER_HAVE_DATA_WORKTREE;

force_shutdown:
	fsmonitor_batch__free_list(batch);
	string_list_clear(&cookie_list, 0);
	strbuf_release(&path);
	return LISTENER_SHUTDOWN;
}

/*
 * Process filesystem events that happened anywhere (recursively) under the
 * external <gitdir> (such as non-primary worktrees or submodules).
 * We only care about cookie files that our client threads created here.
 *
 * Note that we DO NOT get filesystem events on the external <gitdir>
 * itself (it is not inside something that we are watching).  In particular,
 * we do not get an event if the external <gitdir> is deleted.
 *
 * Also, we do not care about shortnames within the external <gitdir>, since
 * we never send these paths to clients.
 */
static int process_gitdir_events(struct fsmonitor_daemon_state *state)
{
	struct fsm_listen_data *data = state->listen_data;
	struct one_watch *watch = data->watch_gitdir;
	struct strbuf path = STRBUF_INIT;
	struct string_list cookie_list = STRING_LIST_INIT_DUP;
	const char *p = watch->buffer;

	if (!watch->count) {
		trace2_data_string("fsmonitor", NULL, "fsm-listen/kernel",
				   "overflow");
		fsmonitor_force_resync(state);
		return LISTENER_HAVE_DATA_GITDIR;
	}

	for (;;) {
		FILE_NOTIFY_INFORMATION *info = (void *)p;
		const char *slash;
		enum fsmonitor_path_type t;

		if (normalize_path_in_utf8(
			    info->FileName,
			    info->FileNameLength / sizeof(WCHAR),
			    &path) == -1)
			goto skip_this_path;

		t = fsmonitor_classify_path_gitdir_relative(path.buf);

		switch (t) {
		case IS_INSIDE_GITDIR_WITH_COOKIE_PREFIX:
			/* special case cookie files within gitdir */

			/* Use just the filename of the cookie file. */
			slash = find_last_dir_sep(path.buf);
			string_list_append(&cookie_list,
					   slash ? slash + 1 : path.buf);
			break;

		case IS_INSIDE_GITDIR:
			goto skip_this_path;

		default:
			BUG("unexpected path classification '%d' for '%s'",
			    t, path.buf);
		}

skip_this_path:
		if (!info->NextEntryOffset)
			break;
		p += info->NextEntryOffset;
	}

	fsmonitor_publish(state, NULL, &cookie_list);
	string_list_clear(&cookie_list, 0);
	strbuf_release(&path);
	return LISTENER_HAVE_DATA_GITDIR;
}

void fsm_listen__loop(struct fsmonitor_daemon_state *state)
{
	struct fsm_listen_data *data = state->listen_data;
	DWORD dwWait;
	int result;

	state->listen_error_code = 0;

	if (start_rdcw_watch(data, data->watch_worktree) == -1)
		goto force_error_stop;

	if (data->watch_gitdir &&
	    start_rdcw_watch(data, data->watch_gitdir) == -1)
		goto force_error_stop;

	for (;;) {
		dwWait = WaitForMultipleObjects(data->nr_listener_handles,
						data->hListener,
						FALSE, INFINITE);

		if (dwWait == WAIT_OBJECT_0 + LISTENER_HAVE_DATA_WORKTREE) {
			result = recv_rdcw_watch(data->watch_worktree);
			if (result == -1) {
				/* hard error */
				goto force_error_stop;
			}
			if (result == -2) {
				/* retryable error */
				if (start_rdcw_watch(data, data->watch_worktree) == -1)
					goto force_error_stop;
				continue;
			}

			/* have data */
			if (process_worktree_events(state) == LISTENER_SHUTDOWN)
				goto force_shutdown;
			if (start_rdcw_watch(data, data->watch_worktree) == -1)
				goto force_error_stop;
			continue;
		}

		if (dwWait == WAIT_OBJECT_0 + LISTENER_HAVE_DATA_GITDIR) {
			result = recv_rdcw_watch(data->watch_gitdir);
			if (result == -1) {
				/* hard error */
				goto force_error_stop;
			}
			if (result == -2) {
				/* retryable error */
				if (start_rdcw_watch(data, data->watch_gitdir) == -1)
					goto force_error_stop;
				continue;
			}

			/* have data */
			if (process_gitdir_events(state) == LISTENER_SHUTDOWN)
				goto force_shutdown;
			if (start_rdcw_watch(data, data->watch_gitdir) == -1)
				goto force_error_stop;
			continue;
		}

		if (dwWait == WAIT_OBJECT_0 + LISTENER_SHUTDOWN)
			goto clean_shutdown;

		error(_("could not read directory changes [GLE %ld]"),
		      GetLastError());
		goto force_error_stop;
	}

force_error_stop:
	state->listen_error_code = -1;

force_shutdown:
	/*
	 * Tell the IPC thead pool to stop (which completes the await
	 * in the main thread (which will also signal this thread (if
	 * we are still alive))).
	 */
	ipc_server_stop_async(state->ipc_server_data);

clean_shutdown:
	cancel_rdcw_watch(data->watch_worktree);
	cancel_rdcw_watch(data->watch_gitdir);
}

int fsm_listen__ctor(struct fsmonitor_daemon_state *state)
{
	struct fsm_listen_data *data;

	CALLOC_ARRAY(data, 1);

	data->hEventShutdown = CreateEvent(NULL, TRUE, FALSE, NULL);

	data->watch_worktree = create_watch(state,
					    state->path_worktree_watch.buf);
	if (!data->watch_worktree)
		goto failed;

	check_for_shortnames(data->watch_worktree);

	if (state->nr_paths_watching > 1) {
		data->watch_gitdir = create_watch(state,
						  state->path_gitdir_watch.buf);
		if (!data->watch_gitdir)
			goto failed;
	}

	data->hListener[LISTENER_SHUTDOWN] = data->hEventShutdown;
	data->nr_listener_handles++;

	data->hListener[LISTENER_HAVE_DATA_WORKTREE] =
		data->watch_worktree->hEvent;
	data->nr_listener_handles++;

	if (data->watch_gitdir) {
		data->hListener[LISTENER_HAVE_DATA_GITDIR] =
			data->watch_gitdir->hEvent;
		data->nr_listener_handles++;
	}

	state->listen_data = data;
	return 0;

failed:
	CloseHandle(data->hEventShutdown);
	destroy_watch(data->watch_worktree);
	destroy_watch(data->watch_gitdir);

	return -1;
}

void fsm_listen__dtor(struct fsmonitor_daemon_state *state)
{
	struct fsm_listen_data *data;

	if (!state || !state->listen_data)
		return;

	data = state->listen_data;

	CloseHandle(data->hEventShutdown);
	destroy_watch(data->watch_worktree);
	destroy_watch(data->watch_gitdir);

	FREE_AND_NULL(state->listen_data);
}
