#ifndef __clang__
/*
 * It is possible to #include CoreFoundation/CoreFoundation.h when compiling
 * with clang, but not with GCC as of time of writing.
 *
 * See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=93082 for details.
 */
typedef unsigned int FSEventStreamCreateFlags;
#define kFSEventStreamEventFlagNone               0x00000000
#define kFSEventStreamEventFlagMustScanSubDirs    0x00000001
#define kFSEventStreamEventFlagUserDropped        0x00000002
#define kFSEventStreamEventFlagKernelDropped      0x00000004
#define kFSEventStreamEventFlagEventIdsWrapped    0x00000008
#define kFSEventStreamEventFlagHistoryDone        0x00000010
#define kFSEventStreamEventFlagRootChanged        0x00000020
#define kFSEventStreamEventFlagMount              0x00000040
#define kFSEventStreamEventFlagUnmount            0x00000080
#define kFSEventStreamEventFlagItemCreated        0x00000100
#define kFSEventStreamEventFlagItemRemoved        0x00000200
#define kFSEventStreamEventFlagItemInodeMetaMod   0x00000400
#define kFSEventStreamEventFlagItemRenamed        0x00000800
#define kFSEventStreamEventFlagItemModified       0x00001000
#define kFSEventStreamEventFlagItemFinderInfoMod  0x00002000
#define kFSEventStreamEventFlagItemChangeOwner    0x00004000
#define kFSEventStreamEventFlagItemXattrMod       0x00008000
#define kFSEventStreamEventFlagItemIsFile         0x00010000
#define kFSEventStreamEventFlagItemIsDir          0x00020000
#define kFSEventStreamEventFlagItemIsSymlink      0x00040000
#define kFSEventStreamEventFlagOwnEvent           0x00080000
#define kFSEventStreamEventFlagItemIsHardlink     0x00100000
#define kFSEventStreamEventFlagItemIsLastHardlink 0x00200000
#define kFSEventStreamEventFlagItemCloned         0x00400000

typedef struct __FSEventStream *FSEventStreamRef;
typedef const FSEventStreamRef ConstFSEventStreamRef;

typedef unsigned int CFStringEncoding;
#define kCFStringEncodingUTF8 0x08000100

typedef const struct __CFString *CFStringRef;
typedef const struct __CFArray *CFArrayRef;
typedef const struct __CFRunLoop *CFRunLoopRef;

struct FSEventStreamContext {
    long long version;
    void *cb_data, *retain, *release, *copy_description;
};

typedef struct FSEventStreamContext FSEventStreamContext;
typedef unsigned int FSEventStreamEventFlags;
#define kFSEventStreamCreateFlagNoDefer 0x02
#define kFSEventStreamCreateFlagWatchRoot 0x04
#define kFSEventStreamCreateFlagFileEvents 0x10

typedef unsigned long long FSEventStreamEventId;
#define kFSEventStreamEventIdSinceNow 0xFFFFFFFFFFFFFFFFULL

typedef void (*FSEventStreamCallback)(ConstFSEventStreamRef streamRef,
				      void *context,
				      __SIZE_TYPE__ num_of_events,
				      void *event_paths,
				      const FSEventStreamEventFlags event_flags[],
				      const FSEventStreamEventId event_ids[]);
typedef double CFTimeInterval;
FSEventStreamRef FSEventStreamCreate(void *allocator,
				     FSEventStreamCallback callback,
				     FSEventStreamContext *context,
				     CFArrayRef paths_to_watch,
				     FSEventStreamEventId since_when,
				     CFTimeInterval latency,
				     FSEventStreamCreateFlags flags);
CFStringRef CFStringCreateWithCString(void *allocator, const char *string,
				      CFStringEncoding encoding);
CFArrayRef CFArrayCreate(void *allocator, const void **items, long long count,
			 void *callbacks);
void CFRunLoopRun(void);
void CFRunLoopStop(CFRunLoopRef run_loop);
CFRunLoopRef CFRunLoopGetCurrent(void);
extern CFStringRef kCFRunLoopDefaultMode;
void FSEventStreamScheduleWithRunLoop(FSEventStreamRef stream,
				      CFRunLoopRef run_loop,
				      CFStringRef run_loop_mode);
unsigned char FSEventStreamStart(FSEventStreamRef stream);
void FSEventStreamStop(FSEventStreamRef stream);
void FSEventStreamInvalidate(FSEventStreamRef stream);
void FSEventStreamRelease(FSEventStreamRef stream);
#else
/*
 * Let Apple's headers declare `isalnum()` first, before
 * Git's headers override it via a constant
 */
#include <string.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#endif

#include "cache.h"
#include "fsmonitor.h"
#include "fsm-listen.h"
#include "fsmonitor--daemon.h"

struct fsmonitor_daemon_backend_data
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

	trace_printf_key(&trace_fsmonitor, "fsevent: '%s', flags=%u %s",
			 path, flag, msg.buf);

	strbuf_release(&msg);
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

static void fsevent_callback(ConstFSEventStreamRef streamRef,
			     void *ctx,
			     size_t num_of_events,
			     void *event_paths,
			     const FSEventStreamEventFlags event_flags[],
			     const FSEventStreamEventId event_ids[])
{
	struct fsmonitor_daemon_state *state = ctx;
	struct fsmonitor_daemon_backend_data *data = state->backend_data;
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
 * NEEDSWORK: Investigate the proper value for the `latency` argument
 * in the call to `FSEventStreamCreate()`.  I'm not sure that this
 * needs to be a config setting or just something that we tune after
 * some testing.
 *
 * With a latency of 0.1, I was seeing lots of dropped events during
 * the "touch 100000" files test within t/perf/p7519, but with a
 * latency of 0.001 I did not see any dropped events.  So the
 * "correct" value may be somewhere in between.
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
	struct fsmonitor_daemon_backend_data *data;
	const void *dir_array[2];

	CALLOC_ARRAY(data, 1);
	state->backend_data = data;

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
	error("Unable to create FSEventStream.");

	FREE_AND_NULL(state->backend_data);
	return -1;
}

void fsm_listen__dtor(struct fsmonitor_daemon_state *state)
{
	struct fsmonitor_daemon_backend_data *data;

	if (!state || !state->backend_data)
		return;

	data = state->backend_data;

	if (data->stream) {
		if (data->stream_started)
			FSEventStreamStop(data->stream);
		if (data->stream_scheduled)
			FSEventStreamInvalidate(data->stream);
		FSEventStreamRelease(data->stream);
	}

	FREE_AND_NULL(state->backend_data);
}

void fsm_listen__stop_async(struct fsmonitor_daemon_state *state)
{
	struct fsmonitor_daemon_backend_data *data;

	data = state->backend_data;
	data->shutdown_style = SHUTDOWN_EVENT;

	CFRunLoopStop(data->rl);
}

void fsm_listen__loop(struct fsmonitor_daemon_state *state)
{
	struct fsmonitor_daemon_backend_data *data;

	data = state->backend_data;

	data->rl = CFRunLoopGetCurrent();

	FSEventStreamScheduleWithRunLoop(data->stream, data->rl, kCFRunLoopDefaultMode);
	data->stream_scheduled = 1;

	if (!FSEventStreamStart(data->stream)) {
		error("Failed to start the FSEventStream");
		goto force_error_stop_without_loop;
	}
	data->stream_started = 1;

	CFRunLoopRun();

	switch (data->shutdown_style) {
	case FORCE_ERROR_STOP:
		state->error_code = -1;
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
	state->error_code = -1;
	ipc_server_stop_async(state->ipc_server_data);
	return;
}
