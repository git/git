#ifndef __clang__
#include "fsm-darwin-gcc.h"
#else
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

#ifndef AVAILABLE_MAC_OS_X_VERSION_10_13_AND_LATER
/*
 * This enum value was added in 10.13 to:
 *
 * /Applications/Xcode.app/Contents/Developer/Platforms/ \
 *    MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/ \
 *    Library/Frameworks/CoreServices.framework/Frameworks/ \
 *    FSEvents.framework/Versions/Current/Headers/FSEvents.h
 *
 * If we're compiling against an older SDK, this symbol won't be
 * present.  Silently define it here so that we don't have to ifdef
 * the logging or masking below.  This should be harmless since older
 * versions of macOS won't ever emit this FS event anyway.
 */
#define kFSEventStreamEventFlagItemCloned         0x00400000
#endif
#endif

#include "cache.h"
#include "fsmonitor.h"
#include "fsm-listen.h"
#include "fsmonitor--daemon.h"

struct fsm_listen_data
{
	CFStringRef cfsr_worktree_path;
	CFStringRef cfsr_gitdir_path;

	CFArrayRef cfar_paths_to_watch;
	int nr_paths_watching;

	FSEventStreamRef stream;

	CFRunLoopRef rl;

	enum shutdown_style {
		SHUTDOWN_EVENT = 0,
		FORCE_SHUTDOWN,
		FORCE_ERROR_STOP,
	} shutdown_style;

	unsigned int stream_scheduled:1;
	unsigned int stream_started:1;
};

static void log_flags_set(const char *path, const FSEventStreamEventFlags flag)
{
	struct strbuf msg = STRBUF_INIT;

	if (flag & kFSEventStreamEventFlagMustScanSubDirs)
		strbuf_addstr(&msg, "MustScanSubDirs|");
	if (flag & kFSEventStreamEventFlagUserDropped)
		strbuf_addstr(&msg, "UserDropped|");
	if (flag & kFSEventStreamEventFlagKernelDropped)
		strbuf_addstr(&msg, "KernelDropped|");
	if (flag & kFSEventStreamEventFlagEventIdsWrapped)
		strbuf_addstr(&msg, "EventIdsWrapped|");
	if (flag & kFSEventStreamEventFlagHistoryDone)
		strbuf_addstr(&msg, "HistoryDone|");
	if (flag & kFSEventStreamEventFlagRootChanged)
		strbuf_addstr(&msg, "RootChanged|");
	if (flag & kFSEventStreamEventFlagMount)
		strbuf_addstr(&msg, "Mount|");
	if (flag & kFSEventStreamEventFlagUnmount)
		strbuf_addstr(&msg, "Unmount|");
	if (flag & kFSEventStreamEventFlagItemChangeOwner)
		strbuf_addstr(&msg, "ItemChangeOwner|");
	if (flag & kFSEventStreamEventFlagItemCreated)
		strbuf_addstr(&msg, "ItemCreated|");
	if (flag & kFSEventStreamEventFlagItemFinderInfoMod)
		strbuf_addstr(&msg, "ItemFinderInfoMod|");
	if (flag & kFSEventStreamEventFlagItemInodeMetaMod)
		strbuf_addstr(&msg, "ItemInodeMetaMod|");
	if (flag & kFSEventStreamEventFlagItemIsDir)
		strbuf_addstr(&msg, "ItemIsDir|");
	if (flag & kFSEventStreamEventFlagItemIsFile)
		strbuf_addstr(&msg, "ItemIsFile|");
	if (flag & kFSEventStreamEventFlagItemIsHardlink)
		strbuf_addstr(&msg, "ItemIsHardlink|");
	if (flag & kFSEventStreamEventFlagItemIsLastHardlink)
		strbuf_addstr(&msg, "ItemIsLastHardlink|");
	if (flag & kFSEventStreamEventFlagItemIsSymlink)
		strbuf_addstr(&msg, "ItemIsSymlink|");
	if (flag & kFSEventStreamEventFlagItemModified)
		strbuf_addstr(&msg, "ItemModified|");
	if (flag & kFSEventStreamEventFlagItemRemoved)
		strbuf_addstr(&msg, "ItemRemoved|");
	if (flag & kFSEventStreamEventFlagItemRenamed)
		strbuf_addstr(&msg, "ItemRenamed|");
	if (flag & kFSEventStreamEventFlagItemXattrMod)
		strbuf_addstr(&msg, "ItemXattrMod|");
	if (flag & kFSEventStreamEventFlagOwnEvent)
		strbuf_addstr(&msg, "OwnEvent|");
	if (flag & kFSEventStreamEventFlagItemCloned)
		strbuf_addstr(&msg, "ItemCloned|");

	trace_printf_key(&trace_fsmonitor, "fsevent: '%s', flags=0x%x %s",
			 path, flag, msg.buf);

	strbuf_release(&msg);
}

static int ef_is_root_changed(const FSEventStreamEventFlags ef)
{
	return (ef & kFSEventStreamEventFlagRootChanged);
}

static int ef_is_root_delete(const FSEventStreamEventFlags ef)
{
	return (ef & kFSEventStreamEventFlagItemIsDir &&
		ef & kFSEventStreamEventFlagItemRemoved);
}

static int ef_is_root_renamed(const FSEventStreamEventFlags ef)
{
	return (ef & kFSEventStreamEventFlagItemIsDir &&
		ef & kFSEventStreamEventFlagItemRenamed);
}

static int ef_is_dropped(const FSEventStreamEventFlags ef)
{
	return (ef & kFSEventStreamEventFlagMustScanSubDirs ||
		ef & kFSEventStreamEventFlagKernelDropped ||
		ef & kFSEventStreamEventFlagUserDropped);
}

/*
 * If an `xattr` change is the only reason we received this event,
 * then silently ignore it.  Git doesn't care about xattr's.  We
 * have to be careful here because the kernel can combine multiple
 * events for a single path.  And because events always have certain
 * bits set, such as `ItemIsFile` or `ItemIsDir`.
 *
 * Return 1 if we should ignore it.
 */
static int ef_ignore_xattr(const FSEventStreamEventFlags ef)
{
	static const FSEventStreamEventFlags mask =
		kFSEventStreamEventFlagItemChangeOwner |
		kFSEventStreamEventFlagItemCreated |
		kFSEventStreamEventFlagItemFinderInfoMod |
		kFSEventStreamEventFlagItemInodeMetaMod |
		kFSEventStreamEventFlagItemModified |
		kFSEventStreamEventFlagItemRemoved |
		kFSEventStreamEventFlagItemRenamed |
		kFSEventStreamEventFlagItemXattrMod |
		kFSEventStreamEventFlagItemCloned;

	return ((ef & mask) == kFSEventStreamEventFlagItemXattrMod);
}

static void fsevent_callback(ConstFSEventStreamRef streamRef,
			     void *ctx,
			     size_t num_of_events,
			     void *event_paths,
			     const FSEventStreamEventFlags event_flags[],
			     const FSEventStreamEventId event_ids[])
{
	struct fsmonitor_daemon_state *state = ctx;
	struct fsm_listen_data *data = state->listen_data;
	char **paths = (char **)event_paths;
	struct fsmonitor_batch *batch = NULL;
	struct string_list cookie_list = STRING_LIST_INIT_DUP;
	const char *path_k;
	const char *slash;
	int k;
	struct strbuf tmp = STRBUF_INIT;

	/*
	 * Build a list of all filesystem changes into a private/local
	 * list and without holding any locks.
	 */
	for (k = 0; k < num_of_events; k++) {
		/*
		 * On Mac, we receive an array of absolute paths.
		 */
		path_k = paths[k];

		/*
		 * If you want to debug FSEvents, log them to GIT_TRACE_FSMONITOR.
		 * Please don't log them to Trace2.
		 *
		 * trace_printf_key(&trace_fsmonitor, "Path: '%s'", path_k);
		 */

		/*
		 * If event[k] is marked as dropped, we assume that we have
		 * lost sync with the filesystem and should flush our cached
		 * data.  We need to:
		 *
		 * [1] Abort/wake any client threads waiting for a cookie and
		 *     flush the cached state data (the current token), and
		 *     create a new token.
		 *
		 * [2] Discard the batch that we were locally building (since
		 *     they are conceptually relative to the just flushed
		 *     token).
		 */
		if (ef_is_dropped(event_flags[k])) {
			if (trace_pass_fl(&trace_fsmonitor))
				log_flags_set(path_k, event_flags[k]);

			fsmonitor_force_resync(state);
			fsmonitor_batch__free_list(batch);
			string_list_clear(&cookie_list, 0);

			/*
			 * We assume that any events that we received
			 * in this callback after this dropped event
			 * may still be valid, so we continue rather
			 * than break.  (And just in case there is a
			 * delete of ".git" hiding in there.)
			 */
			continue;
		}

		if (ef_is_root_changed(event_flags[k])) {
			/*
			 * The spelling of the pathname of the root directory
			 * has changed.  This includes the name of the root
			 * directory itself or of any parent directory in the
			 * path.
			 *
			 * (There may be other conditions that throw this,
			 * but I couldn't find any information on it.)
			 *
			 * Force a shutdown now and avoid things getting
			 * out of sync.  The Unix domain socket is inside
			 * the .git directory and a spelling change will make
			 * it hard for clients to rendezvous with us.
			 */
			trace_printf_key(&trace_fsmonitor,
					 "event: root changed");
			goto force_shutdown;
		}

		if (ef_ignore_xattr(event_flags[k])) {
			trace_printf_key(&trace_fsmonitor,
					 "ignore-xattr: '%s', flags=0x%x",
					 path_k, event_flags[k]);
			continue;
		}

		switch (fsmonitor_classify_path_absolute(state, path_k)) {

		case IS_INSIDE_DOT_GIT_WITH_COOKIE_PREFIX:
		case IS_INSIDE_GITDIR_WITH_COOKIE_PREFIX:
			/* special case cookie files within .git or gitdir */

			/* Use just the filename of the cookie file. */
			slash = find_last_dir_sep(path_k);
			string_list_append(&cookie_list,
					   slash ? slash + 1 : path_k);
			break;

		case IS_INSIDE_DOT_GIT:
		case IS_INSIDE_GITDIR:
			/* ignore all other paths inside of .git or gitdir */
			break;

		case IS_DOT_GIT:
		case IS_GITDIR:
			/*
			 * If .git directory is deleted or renamed away,
			 * we have to quit.
			 */
			if (ef_is_root_delete(event_flags[k])) {
				trace_printf_key(&trace_fsmonitor,
						 "event: gitdir removed");
				goto force_shutdown;
			}
			if (ef_is_root_renamed(event_flags[k])) {
				trace_printf_key(&trace_fsmonitor,
						 "event: gitdir renamed");
				goto force_shutdown;
			}
			break;

		case IS_WORKDIR_PATH:
			/* try to queue normal pathnames */

			if (trace_pass_fl(&trace_fsmonitor))
				log_flags_set(path_k, event_flags[k]);

			/*
			 * Because of the implicit "binning" (the
			 * kernel calls us at a given frequency) and
			 * de-duping (the kernel is free to combine
			 * multiple events for a given pathname), an
			 * individual fsevent could be marked as both
			 * a file and directory.  Add it to the queue
			 * with both spellings so that the client will
			 * know how much to invalidate/refresh.
			 */

			if (event_flags[k] & kFSEventStreamEventFlagItemIsFile) {
				const char *rel = path_k +
					state->path_worktree_watch.len + 1;

				if (!batch)
					batch = fsmonitor_batch__new();
				fsmonitor_batch__add_path(batch, rel);
			}

			if (event_flags[k] & kFSEventStreamEventFlagItemIsDir) {
				const char *rel = path_k +
					state->path_worktree_watch.len + 1;

				strbuf_reset(&tmp);
				strbuf_addstr(&tmp, rel);
				strbuf_addch(&tmp, '/');

				if (!batch)
					batch = fsmonitor_batch__new();
				fsmonitor_batch__add_path(batch, tmp.buf);
			}

			break;

		case IS_OUTSIDE_CONE:
		default:
			trace_printf_key(&trace_fsmonitor,
					 "ignoring '%s'", path_k);
			break;
		}
	}

	fsmonitor_publish(state, batch, &cookie_list);
	string_list_clear(&cookie_list, 0);
	strbuf_release(&tmp);
	return;

force_shutdown:
	fsmonitor_batch__free_list(batch);
	string_list_clear(&cookie_list, 0);

	data->shutdown_style = FORCE_SHUTDOWN;
	CFRunLoopStop(data->rl);
	strbuf_release(&tmp);
	return;
}

/*
 * In the call to `FSEventStreamCreate()` to setup our watch, the
 * `latency` argument determines the frequency of calls to our callback
 * with new FS events.  Too slow and events get dropped; too fast and
 * we burn CPU unnecessarily.  Since it is rather obscure, I don't
 * think this needs to be a config setting.  I've done extensive
 * testing on my systems and chosen the value below.  It gives good
 * results and I've not seen any dropped events.
 *
 * With a latency of 0.1, I was seeing lots of dropped events during
 * the "touch 100000" files test within t/perf/p7519, but with a
 * latency of 0.001 I did not see any dropped events.  So I'm going
 * to assume that this is the "correct" value.
 *
 * https://developer.apple.com/documentation/coreservices/1443980-fseventstreamcreate
 */

int fsm_listen__ctor(struct fsmonitor_daemon_state *state)
{
	FSEventStreamCreateFlags flags = kFSEventStreamCreateFlagNoDefer |
		kFSEventStreamCreateFlagWatchRoot |
		kFSEventStreamCreateFlagFileEvents;
	FSEventStreamContext ctx = {
		0,
		state,
		NULL,
		NULL,
		NULL
	};
	struct fsm_listen_data *data;
	const void *dir_array[2];

	CALLOC_ARRAY(data, 1);
	state->listen_data = data;

	data->cfsr_worktree_path = CFStringCreateWithCString(
		NULL, state->path_worktree_watch.buf, kCFStringEncodingUTF8);
	dir_array[data->nr_paths_watching++] = data->cfsr_worktree_path;

	if (state->nr_paths_watching > 1) {
		data->cfsr_gitdir_path = CFStringCreateWithCString(
			NULL, state->path_gitdir_watch.buf,
			kCFStringEncodingUTF8);
		dir_array[data->nr_paths_watching++] = data->cfsr_gitdir_path;
	}

	data->cfar_paths_to_watch = CFArrayCreate(NULL, dir_array,
						  data->nr_paths_watching,
						  NULL);
	data->stream = FSEventStreamCreate(NULL, fsevent_callback, &ctx,
					   data->cfar_paths_to_watch,
					   kFSEventStreamEventIdSinceNow,
					   0.001, flags);
	if (data->stream == NULL)
		goto failed;

	/*
	 * `data->rl` needs to be set inside the listener thread.
	 */

	return 0;

failed:
	error(_("Unable to create FSEventStream."));

	FREE_AND_NULL(state->listen_data);
	return -1;
}

void fsm_listen__dtor(struct fsmonitor_daemon_state *state)
{
	struct fsm_listen_data *data;

	if (!state || !state->listen_data)
		return;

	data = state->listen_data;

	if (data->stream) {
		if (data->stream_started)
			FSEventStreamStop(data->stream);
		if (data->stream_scheduled)
			FSEventStreamInvalidate(data->stream);
		FSEventStreamRelease(data->stream);
	}

	FREE_AND_NULL(state->listen_data);
}

void fsm_listen__stop_async(struct fsmonitor_daemon_state *state)
{
	struct fsm_listen_data *data;

	data = state->listen_data;
	data->shutdown_style = SHUTDOWN_EVENT;

	CFRunLoopStop(data->rl);
}

void fsm_listen__loop(struct fsmonitor_daemon_state *state)
{
	struct fsm_listen_data *data;

	data = state->listen_data;

	data->rl = CFRunLoopGetCurrent();

	FSEventStreamScheduleWithRunLoop(data->stream, data->rl, kCFRunLoopDefaultMode);
	data->stream_scheduled = 1;

	if (!FSEventStreamStart(data->stream)) {
		error(_("Failed to start the FSEventStream"));
		goto force_error_stop_without_loop;
	}
	data->stream_started = 1;

	CFRunLoopRun();

	switch (data->shutdown_style) {
	case FORCE_ERROR_STOP:
		state->listen_error_code = -1;
		/* fall thru */
	case FORCE_SHUTDOWN:
		ipc_server_stop_async(state->ipc_server_data);
		/* fall thru */
	case SHUTDOWN_EVENT:
	default:
		break;
	}
	return;

force_error_stop_without_loop:
	state->listen_error_code = -1;
	ipc_server_stop_async(state->ipc_server_data);
	return;
}
