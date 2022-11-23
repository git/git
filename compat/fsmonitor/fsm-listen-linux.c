#include "cache.h"
#include "fsmonitor.h"
#include "fsm-listen.h"
#include "fsmonitor--daemon.h"
#include <dirent.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>

/*
 * Safe value to bitwise OR with rest of mask for
 * kernels that do not support IN_MASK_CREATE
 */
#ifndef IN_MASK_CREATE
#define IN_MASK_CREATE 0x00000000
#endif

enum shutdown_reason {
	SHUTDOWN_CONTINUE = 0,
	SHUTDOWN_STOP,
	SHUTDOWN_ERROR,
	SHUTDOWN_FORCE
};

struct watch_entry {
	struct hashmap_entry ent;
	int wd;
	uint32_t cookie;
	const char *dir;
};

struct rename_entry {
	struct hashmap_entry ent;
	time_t whence;
	uint32_t cookie;
	const char *dir;
};

struct fsm_listen_data {
	int fd_inotify;
	enum shutdown_reason shutdown;
	struct hashmap watches;
	struct hashmap renames;
	struct hashmap revwatches;
};

static int watch_entry_cmp(const void *cmp_data,
			  const struct hashmap_entry *eptr,
			  const struct hashmap_entry *entry_or_key,
			  const void *keydata)
{
	const struct watch_entry *e1, *e2;

	e1 = container_of(eptr, const struct watch_entry, ent);
	e2 = container_of(eptr, const struct watch_entry, ent);
	return e1->wd != e2->wd;
}

static int revwatches_entry_cmp(const void *cmp_data,
			  const struct hashmap_entry *eptr,
			  const struct hashmap_entry *entry_or_key,
			  const void *keydata)
{
	const struct watch_entry *e1, *e2;

	e1 = container_of(eptr, const struct watch_entry, ent);
	e2 = container_of(eptr, const struct watch_entry, ent);
	return strcmp(e1->dir, e2->dir);
}

static int rename_entry_cmp(const void *cmp_data,
			  const struct hashmap_entry *eptr,
			  const struct hashmap_entry *entry_or_key,
			  const void *keydata)
{
	const struct rename_entry *e1, *e2;

	e1 = container_of(eptr, const struct rename_entry, ent);
	e2 = container_of(eptr, const struct rename_entry, ent);
	return e1->cookie != e2->cookie;
}

/*
 * Register an inotify watch, add watch descriptor to path mapping
 * and the reverse mapping.
 */
static int add_watch(const char *path, struct fsm_listen_data *data)
{
	const char *interned = strintern(path);
	struct watch_entry *w1, *w2;

	/* add the inotify watch, don't allow watches to be modified */
	int wd = inotify_add_watch(data->fd_inotify, interned,
				(IN_ALL_EVENTS | IN_ONLYDIR | IN_MASK_CREATE)
				^ IN_ACCESS ^ IN_CLOSE ^ IN_OPEN);
	if (wd < 0)
		return error_errno("inotify_add_watch('%s') failed", interned);

	/* add watch descriptor -> directory mapping */
	CALLOC_ARRAY(w1, 1);
	w1->wd = wd;
	w1->dir = interned;
	hashmap_entry_init(&w1->ent, memhash(&w1->wd, sizeof(int)));
	hashmap_add(&data->watches, &w1->ent);

	/* add directory -> watch descriptor mapping */
	CALLOC_ARRAY(w2, 1);
	w2->wd = wd;
	w2->dir = interned;
	hashmap_entry_init(&w2->ent, memhash(w2->dir, strlen(w2->dir)));
	hashmap_add(&data->revwatches, &w2->ent);

	return 0;
}

/*
 * Remove the inotify watch, the watch descriptor to path mapping
 * and the reverse mapping.
 */
static void remove_watch(struct watch_entry *w,
	struct fsm_listen_data *data)
{
	struct watch_entry k1, k2, *w1, *w2;

	/* remove watch, ignore error if kernel already did it */
	if (inotify_rm_watch(data->fd_inotify, w->wd) && errno != EINVAL)
		error_errno("inotify_rm_watch() failed");

	hashmap_entry_init(&k1.ent, memhash(&w->wd, sizeof(int)));
	w1 = hashmap_remove_entry(&data->watches, &k1, ent, NULL);
	if (!w1)
		BUG("Double remove of watch for '%s'", w->dir);

	if (w1->cookie)
		BUG("Removing watch for '%s' which has a pending rename", w1->dir);

	hashmap_entry_init(&k2.ent, memhash(w->dir, strlen(w->dir)));
	w2 = hashmap_remove_entry(&data->revwatches, &k2, ent, NULL);
	if (!w2)
		BUG("Double remove of reverse watch for '%s'", w->dir);

	/* w1->dir and w2->dir are interned strings, we don't own them */
	free(w1);
	free(w2);
}

/*
 * Check for stale directory renames.
 *
 * https://man7.org/linux/man-pages/man7/inotify.7.html
 *
 * Allow for some small timeout to account for the fact that insertion of the
 * IN_MOVED_FROM+IN_MOVED_TO event pair is not atomic, and the possibility that
 * there may not be any IN_MOVED_TO event.
 *
 * If the IN_MOVED_TO event is not received within the timeout then events have
 * been missed and the monitor is in an inconsistent state with respect to the
 * filesystem.
 */
static int check_stale_dir_renames(struct hashmap *renames, time_t max_age)
{
	struct rename_entry *re;
	struct hashmap_iter iter;

	hashmap_for_each_entry(renames, &iter, re, ent) {
		if (re->whence <= max_age)
			return -1;
	}
	return 0;
}

/*
 * Track pending renames.
 *
 * Tracking is done via a event cookie to watch descriptor mapping.
 *
 * A rename is not complete until matching a IN_MOVED_TO event is received
 * for a corresponding IN_MOVED_FROM event.
 */
static void add_dir_rename(uint32_t cookie, const char *path,
	struct fsm_listen_data *data)
{
	struct watch_entry k, *w;
	struct rename_entry *re;

	/* lookup the watch descriptor for the given path */
	hashmap_entry_init(&k.ent, memhash(path, strlen(path)));
	w = hashmap_get_entry(&data->revwatches, &k, ent, NULL);
	if (!w) /* should never happen */
		BUG("No watch for '%s'", path);
	w->cookie = cookie;

	/* add the pending rename to match against later */
	CALLOC_ARRAY(re, 1);
	re->dir = w->dir;
	re->cookie = w->cookie;
	re->whence = time(NULL);
	hashmap_entry_init(&re->ent, memhash(&re->cookie, sizeof(uint32_t)));
	hashmap_add(&data->renames, &re->ent);
}

/*
 * Handle directory renames
 *
 * Once a IN_MOVED_TO event is received, lookup the rename tracking information
 * via the event cookie and use this information to update the watch.
 */
static void rename_dir(uint32_t cookie, const char *path,
	struct fsm_listen_data *data)
{
	struct rename_entry rek, *re;
	struct watch_entry k, *w;

	/* lookup a pending rename to match */
	rek.cookie = cookie;
	hashmap_entry_init(&rek.ent, memhash(&rek.cookie, sizeof(uint32_t)));
	re = hashmap_get_entry(&data->renames, &rek, ent, NULL);
	if (re) {
		k.dir = re->dir;
		hashmap_entry_init(&k.ent, memhash(k.dir, strlen(k.dir)));
		w = hashmap_get_entry(&data->revwatches, &k, ent, NULL);
		if (w) {
			w->cookie = 0; /* rename handled */
			remove_watch(w, data);
			add_watch(path, data);
		} else {
			BUG("No matching watch");
		}
	} else {
		BUG("No matching cookie");
	}
}

/*
 * Recursively add watches to every directory under path
 */
static int register_inotify(const char *path,
	struct fsmonitor_daemon_state *state,
	struct fsmonitor_batch *batch)
{
	DIR *dir;
	const char *rel;
	struct strbuf current = STRBUF_INIT;
	struct dirent *de;
	struct stat fs;
	int ret = -1;

	dir = opendir(path);
	if (!dir)
		return error_errno("opendir('%s') failed", path);

	while ((de = readdir_skip_dot_and_dotdot(dir)) != NULL) {
		strbuf_reset(&current);
		strbuf_addf(&current, "%s/%s", path, de->d_name);
		if (lstat(current.buf, &fs)) {
			error_errno("lstat('%s') failed", current.buf);
			goto failed;
		}

		/* recurse into directory */
		if (S_ISDIR(fs.st_mode)) {
			if (add_watch(current.buf, state->listen_data))
				goto failed;
			if (register_inotify(current.buf, state, batch))
				goto failed;
		} else if (batch) {
			rel = current.buf + state->path_worktree_watch.len + 1;
			trace_printf_key(&trace_fsmonitor, "explicitly adding '%s'", rel);
			fsmonitor_batch__add_path(batch, rel);
		}
	}
	ret = 0;

failed:
	strbuf_release(&current);
	if (closedir(dir) < 0)
		return error_errno("closedir('%s') failed", path);
	return ret;
}

static int em_rename_dir_from(u_int32_t mask)
{
	return ((mask & IN_ISDIR) && (mask & IN_MOVED_FROM));
}

static int em_rename_dir_to(u_int32_t mask)
{
	return ((mask & IN_ISDIR) && (mask & IN_MOVED_TO));
}

static int em_remove_watch(u_int32_t mask)
{
	return (mask & IN_DELETE_SELF);
}

static int em_dir_renamed(u_int32_t mask)
{
	return ((mask & IN_ISDIR) && (mask & IN_MOVE));
}

static int em_dir_created(u_int32_t mask)
{
	return ((mask & IN_ISDIR) && (mask & IN_CREATE));
}

static int em_dir_deleted(uint32_t mask)
{
	return ((mask & IN_ISDIR) && (mask & IN_DELETE));
}

static int em_force_shutdown(u_int32_t mask)
{
	return (mask & IN_UNMOUNT) || (mask & IN_Q_OVERFLOW);
}

static int em_ignore(u_int32_t mask)
{
	return (mask & IN_IGNORED) || (mask & IN_MOVE_SELF);
}

static void log_mask_set(const char *path, u_int32_t mask)
{
	struct strbuf msg = STRBUF_INIT;

	if (mask & IN_ACCESS)
		strbuf_addstr(&msg, "IN_ACCESS|");
	if (mask & IN_MODIFY)
		strbuf_addstr(&msg, "IN_MODIFY|");
	if (mask & IN_ATTRIB)
		strbuf_addstr(&msg, "IN_ATTRIB|");
	if (mask & IN_CLOSE_WRITE)
		strbuf_addstr(&msg, "IN_CLOSE_WRITE|");
	if (mask & IN_CLOSE_NOWRITE)
		strbuf_addstr(&msg, "IN_CLOSE_NOWRITE|");
	if (mask & IN_OPEN)
		strbuf_addstr(&msg, "IN_OPEN|");
	if (mask & IN_MOVED_FROM)
		strbuf_addstr(&msg, "IN_MOVED_FROM|");
	if (mask & IN_MOVED_TO)
		strbuf_addstr(&msg, "IN_MOVED_TO|");
	if (mask & IN_CREATE)
		strbuf_addstr(&msg, "IN_CREATE|");
	if (mask & IN_DELETE)
		strbuf_addstr(&msg, "IN_DELETE|");
	if (mask & IN_DELETE_SELF)
		strbuf_addstr(&msg, "IN_DELETE_SELF|");
	if (mask & IN_MOVE_SELF)
		strbuf_addstr(&msg, "IN_MOVE_SELF|");
	if (mask & IN_UNMOUNT)
		strbuf_addstr(&msg, "IN_UNMOUNT|");
	if (mask & IN_Q_OVERFLOW)
		strbuf_addstr(&msg, "IN_Q_OVERFLOW|");
	if (mask & IN_IGNORED)
		strbuf_addstr(&msg, "IN_IGNORED|");
	if (mask & IN_ISDIR)
		strbuf_addstr(&msg, "IN_ISDIR|");

	trace_printf_key(&trace_fsmonitor, "inotify_event: '%s', mask=%#8.8x %s",
				path, mask, msg.buf);

	strbuf_release(&msg);
}

int fsm_listen__ctor(struct fsmonitor_daemon_state *state)
{
	int fd;
	int ret = 0;
	struct fsm_listen_data *data;

	CALLOC_ARRAY(data, 1);
	state->listen_data = data;
	state->listen_error_code = -1;
	data->shutdown = SHUTDOWN_ERROR;

	fd = inotify_init1(O_NONBLOCK);
	if (fd < 0)
		return error_errno("inotify_init1() failed");

	data->fd_inotify = fd;

	hashmap_init(&data->watches, watch_entry_cmp, NULL, 0);
	hashmap_init(&data->renames, rename_entry_cmp, NULL, 0);
	hashmap_init(&data->revwatches, revwatches_entry_cmp, NULL, 0);

	if (add_watch(state->path_worktree_watch.buf, data))
		ret = -1;
	else if (register_inotify(state->path_worktree_watch.buf, state, NULL))
		ret = -1;
	else if (state->nr_paths_watching > 1) {
		if (add_watch(state->path_gitdir_watch.buf, data))
			ret = -1;
		else if (register_inotify(state->path_gitdir_watch.buf, state, NULL))
			ret = -1;
	}

	if (!ret) {
		state->listen_error_code = 0;
		data->shutdown = SHUTDOWN_CONTINUE;
	}

	return ret;
}

void fsm_listen__dtor(struct fsmonitor_daemon_state *state)
{
	struct fsm_listen_data *data;
	struct hashmap_iter iter;
	struct watch_entry *w;
	int fd;

	if (!state || !state->listen_data)
		return;

	data = state->listen_data;
	fd = data->fd_inotify;

	hashmap_for_each_entry(&data->watches, &iter, w, ent) {
		w->cookie = 0; /* ignore any pending renames */
		remove_watch(w, data);
	}
	hashmap_clear(&data->watches);

	hashmap_clear(&data->revwatches); /* remove_watch freed the entries */

	hashmap_clear_and_free(&data->renames, struct rename_entry, ent);

	FREE_AND_NULL(state->listen_data);

	if (fd && (close(fd) < 0))
		error_errno(_("closing inotify file descriptor failed"));
}

void fsm_listen__stop_async(struct fsmonitor_daemon_state *state)
{
	if (!state->listen_data->shutdown)
		state->listen_data->shutdown = SHUTDOWN_STOP;
}

/*
 * Process a single inotify event and queue for publication.
 */
static int process_event(const char *path,
	const struct inotify_event *event,
	struct fsmonitor_batch *batch,
	struct string_list *cookie_list,
	struct fsmonitor_daemon_state *state)
{
	const char *rel;
	const char *last_sep;

	switch (fsmonitor_classify_path_absolute(state, path)) {
		case IS_INSIDE_DOT_GIT_WITH_COOKIE_PREFIX:
		case IS_INSIDE_GITDIR_WITH_COOKIE_PREFIX:
			/* Use just the filename of the cookie file. */
			last_sep = find_last_dir_sep(path);
			string_list_append(cookie_list,
					last_sep ? last_sep + 1 : path);
			break;
		case IS_INSIDE_DOT_GIT:
		case IS_INSIDE_GITDIR:
			break;
		case IS_DOT_GIT:
		case IS_GITDIR:
			/*
			* If .git directory is deleted or renamed away,
			* we have to quit.
			*/
			if (em_dir_deleted(event->mask)) {
				trace_printf_key(&trace_fsmonitor,
						"event: gitdir removed");
				state->listen_data->shutdown = SHUTDOWN_FORCE;
				goto done;
			}

			if (em_dir_renamed(event->mask)) {
				trace_printf_key(&trace_fsmonitor,
						"event: gitdir renamed");
				state->listen_data->shutdown = SHUTDOWN_FORCE;
				goto done;
			}
			break;
		case IS_WORKDIR_PATH:
			/* normal events in the working directory */
			if (trace_pass_fl(&trace_fsmonitor))
				log_mask_set(path, event->mask);

			rel = path + state->path_worktree_watch.len + 1;
			fsmonitor_batch__add_path(batch, rel);

			if (em_dir_deleted(event->mask))
				break;

			/* received IN_MOVE_FROM, add tracking for expected IN_MOVE_TO */
			if (em_rename_dir_from(event->mask))
				add_dir_rename(event->cookie, path, state->listen_data);

			/* received IN_MOVE_TO, update watch to reflect new path */
			if (em_rename_dir_to(event->mask)) {
				rename_dir(event->cookie, path, state->listen_data);
				if (register_inotify(path, state, batch)) {
					state->listen_data->shutdown = SHUTDOWN_ERROR;
					goto done;
				}
			}

			if (em_dir_created(event->mask)) {
				if (add_watch(path, state->listen_data)) {
					state->listen_data->shutdown = SHUTDOWN_ERROR;
					goto done;
				}
				if (register_inotify(path, state, batch)) {
					state->listen_data->shutdown = SHUTDOWN_ERROR;
					goto done;
				}
			}
			break;
		case IS_OUTSIDE_CONE:
		default:
			trace_printf_key(&trace_fsmonitor,
					"ignoring '%s'", path);
			break;
	}
	return 0;
done:
	return -1;
}

/*
 * Read the inotify event stream and pre-process events before further
 * processing and eventual publishing.
 */
static void handle_events(struct fsmonitor_daemon_state *state)
{
	 /* See https://man7.org/linux/man-pages/man7/inotify.7.html */
	char buf[4096]
		__attribute__ ((aligned(__alignof__(struct inotify_event))));

	struct hashmap watches = state->listen_data->watches;
	struct fsmonitor_batch *batch = NULL;
	struct string_list cookie_list = STRING_LIST_INIT_DUP;
	struct watch_entry k, *w;
	struct strbuf path;
	const struct inotify_event *event;
	int fd = state->listen_data->fd_inotify;
	ssize_t len;
	char *ptr, *p;

	strbuf_init(&path, PATH_MAX);

	for(;;) {
		len = read(fd, buf, sizeof(buf));
		if (len == -1 && errno != EAGAIN) {
			error_errno(_("reading inotify message stream failed"));
			state->listen_data->shutdown = SHUTDOWN_ERROR;
			goto done;
		}

		/* nothing to read */
		if (len <= 0)
			goto done;

		/* Loop over all events in the buffer. */
		for (ptr = buf; ptr < buf + len;
			 ptr += sizeof(struct inotify_event) + event->len) {

			event = (const struct inotify_event *) ptr;

			if (em_ignore(event->mask))
				continue;

			/* File system was unmounted or event queue overflowed */
			if (em_force_shutdown(event->mask)) {
				if (trace_pass_fl(&trace_fsmonitor))
					log_mask_set("Forcing shutdown", event->mask);
				state->listen_data->shutdown = SHUTDOWN_FORCE;
				goto done;
			}

			hashmap_entry_init(&k.ent, memhash(&event->wd, sizeof(int)));
			k.wd = event->wd;

			w = hashmap_get_entry(&watches, &k, ent, NULL);
			if (!w) /* should never happen */
				BUG("No watch for '%s'", event->name);

			/* directory watch was removed */
			if (em_remove_watch(event->mask)) {
				remove_watch(w, state->listen_data);
				continue;
			}

			strbuf_reset(&path);
			strbuf_add(&path, w->dir, strlen(w->dir));
			strbuf_addch(&path, '/');
			strbuf_addstr(&path, event->name);

			p = fsmonitor__resolve_alias(path.buf, &state->alias);
			if (!p)
				p = strbuf_detach(&path, NULL);

			if (!batch)
				batch = fsmonitor_batch__new();

			if (process_event(p, event, batch, &cookie_list, state)) {
				free(p);
				goto done;
			}
			free(p);
		}
		strbuf_reset(&path);
		fsmonitor_publish(state, batch, &cookie_list);
		string_list_clear(&cookie_list, 0);
		batch = NULL;
	}
done:
	strbuf_release(&path);
	fsmonitor_batch__free_list(batch);
	string_list_clear(&cookie_list, 0);
}

/*
 * Non-blocking read of the inotify events stream. The inotify fd is polled
 * frequently to help minimize the number of queue overflows.
 */
void fsm_listen__loop(struct fsmonitor_daemon_state *state)
{
	int poll_num;
	const int interval = 1000;
	time_t checked = time(NULL);
	struct pollfd fds[1];
	fds[0].fd = state->listen_data->fd_inotify;
	fds[0].events = POLLIN;

	for(;;) {
		switch (state->listen_data->shutdown) {
			case SHUTDOWN_CONTINUE:
				poll_num = poll(fds, 1, 1);
				if (poll_num == -1) {
					if (errno == EINTR)
						continue;
					error_errno(_("polling inotify message stream failed"));
					state->listen_data->shutdown = SHUTDOWN_ERROR;
					continue;
				}

				if ((time(NULL) - checked) >= interval) {
					checked = time(NULL);
					if (check_stale_dir_renames(&state->listen_data->renames,
						checked - interval)) {
						trace_printf_key(&trace_fsmonitor,
							"Missed IN_MOVED_TO events, forcing shutdown");
						state->listen_data->shutdown = SHUTDOWN_FORCE;
						continue;
					}
				}

				if (poll_num > 0 && (fds[0].revents & POLLIN))
					handle_events(state);

				continue;
			case SHUTDOWN_ERROR:
				state->listen_error_code = -1;
				ipc_server_stop_async(state->ipc_server_data);
				break;
			case SHUTDOWN_FORCE:
				state->listen_error_code = 0;
				ipc_server_stop_async(state->ipc_server_data);
				break;
			case SHUTDOWN_STOP:
			default:
				state->listen_error_code = 0;
				break;
		}
		return;
	}
}
