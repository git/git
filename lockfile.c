/*
 * Copyright (c) 2005, Junio C Hamano
 */

/*
 * State diagram and cleanup
 * -------------------------
 *
 * This module keeps track of all locked files in `lock_file_list` for
 * use at cleanup. This list and the `lock_file` objects that comprise
 * it must be kept in self-consistent states at all time, because the
 * program can be interrupted any time by a signal, in which case the
 * signal handler will walk through the list attempting to clean up
 * any open lock files.
 *
 * The possible states of a `lock_file` object are as follows:
 *
 * - Uninitialized. In this state the object's `on_list` field must be
 *   zero but the rest of its contents need not be initialized. As
 *   soon as the object is used in any way, it is irrevocably
 *   registered in `lock_file_list`, and `on_list` is set.
 *
 * - Locked, lockfile open (after `hold_lock_file_for_update()`,
 *   `hold_lock_file_for_append()`, or `reopen_lock_file()`). In this
 *   state:
 *
 *   - the lockfile exists
 *   - `active` is set
 *   - `filename` holds the filename of the lockfile
 *   - `fd` holds a file descriptor open for writing to the lockfile
 *   - `fp` holds a pointer to an open `FILE` object if and only if
 *     `fdopen_lock_file()` has been called on the object
 *   - `owner` holds the PID of the process that locked the file
 *
 * - Locked, lockfile closed (after successful `close_lock_file()`).
 *   Same as the previous state, except that the lockfile is closed
 *   and `fd` is -1.
 *
 * - Unlocked (after `commit_lock_file()`, `commit_lock_file_to()`,
 *   `rollback_lock_file()`, a failed attempt to lock, or a failed
 *   `close_lock_file()`).  In this state:
 *
 *   - `active` is unset
 *   - `filename` is empty (usually, though there are transitory
 *     states in which this condition doesn't hold). Client code should
 *     *not* rely on the filename being empty in this state.
 *   - `fd` is -1
 *   - the object is left registered in the `lock_file_list`, and
 *     `on_list` is set.
 *
 * A lockfile is owned by the process that created it. The `lock_file`
 * has an `owner` field that records the owner's PID. This field is
 * used to prevent a forked process from closing a lockfile created by
 * its parent.
 */

#include "cache.h"
#include "lockfile.h"
#include "sigchain.h"

static struct lock_file *volatile lock_file_list;

static void remove_lock_files(int skip_fclose)
{
	pid_t me = getpid();

	while (lock_file_list) {
		if (lock_file_list->owner == me) {
			/* fclose() is not safe to call in a signal handler */
			if (skip_fclose)
				lock_file_list->fp = NULL;
			rollback_lock_file(lock_file_list);
		}
		lock_file_list = lock_file_list->next;
	}
}

static void remove_lock_files_on_exit(void)
{
	remove_lock_files(0);
}

static void remove_lock_files_on_signal(int signo)
{
	remove_lock_files(1);
	sigchain_pop(signo);
	raise(signo);
}

/*
 * path = absolute or relative path name
 *
 * Remove the last path name element from path (leaving the preceding
 * "/", if any).  If path is empty or the root directory ("/"), set
 * path to the empty string.
 */
static void trim_last_path_component(struct strbuf *path)
{
	int i = path->len;

	/* back up past trailing slashes, if any */
	while (i && path->buf[i - 1] == '/')
		i--;

	/*
	 * then go backwards until a slash, or the beginning of the
	 * string
	 */
	while (i && path->buf[i - 1] != '/')
		i--;

	strbuf_setlen(path, i);
}


/* We allow "recursive" symbolic links. Only within reason, though */
#define MAXDEPTH 5

/*
 * path contains a path that might be a symlink.
 *
 * If path is a symlink, attempt to overwrite it with a path to the
 * real file or directory (which may or may not exist), following a
 * chain of symlinks if necessary.  Otherwise, leave path unmodified.
 *
 * This is a best-effort routine.  If an error occurs, path will
 * either be left unmodified or will name a different symlink in a
 * symlink chain that started with the original path.
 */
static void resolve_symlink(struct strbuf *path)
{
	int depth = MAXDEPTH;
	static struct strbuf link = STRBUF_INIT;

	while (depth--) {
		if (strbuf_readlink(&link, path->buf, path->len) < 0)
			break;

		if (is_absolute_path(link.buf))
			/* absolute path simply replaces p */
			strbuf_reset(path);
		else
			/*
			 * link is a relative path, so replace the
			 * last element of p with it.
			 */
			trim_last_path_component(path);

		strbuf_addbuf(path, &link);
	}
	strbuf_reset(&link);
}

/* Make sure errno contains a meaningful value on error */
static int lock_file(struct lock_file *lk, const char *path, int flags)
{
	size_t pathlen = strlen(path);

	if (!lock_file_list) {
		/* One-time initialization */
		sigchain_push_common(remove_lock_files_on_signal);
		atexit(remove_lock_files_on_exit);
	}

	if (lk->active)
		die("BUG: cannot lock_file(\"%s\") using active struct lock_file",
		    path);
	if (!lk->on_list) {
		/* Initialize *lk and add it to lock_file_list: */
		lk->fd = -1;
		lk->fp = NULL;
		lk->active = 0;
		lk->owner = 0;
		strbuf_init(&lk->filename, pathlen + LOCK_SUFFIX_LEN);
		lk->next = lock_file_list;
		lock_file_list = lk;
		lk->on_list = 1;
	} else if (lk->filename.len) {
		/* This shouldn't happen, but better safe than sorry. */
		die("BUG: lock_file(\"%s\") called with improperly-reset lock_file object",
		    path);
	}

	if (flags & LOCK_NO_DEREF) {
		strbuf_add_absolute_path(&lk->filename, path);
	} else {
		struct strbuf resolved_path = STRBUF_INIT;

		strbuf_add(&resolved_path, path, pathlen);
		resolve_symlink(&resolved_path);
		strbuf_add_absolute_path(&lk->filename, resolved_path.buf);
		strbuf_release(&resolved_path);
	}

	strbuf_addstr(&lk->filename, LOCK_SUFFIX);
	lk->fd = open(lk->filename.buf, O_RDWR | O_CREAT | O_EXCL, 0666);
	if (lk->fd < 0) {
		strbuf_reset(&lk->filename);
		return -1;
	}
	lk->owner = getpid();
	lk->active = 1;
	if (adjust_shared_perm(lk->filename.buf)) {
		int save_errno = errno;
		error("cannot fix permission bits on %s", lk->filename.buf);
		rollback_lock_file(lk);
		errno = save_errno;
		return -1;
	}
	return lk->fd;
}

static int sleep_microseconds(long us)
{
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = us;
	return select(0, NULL, NULL, NULL, &tv);
}

/*
 * Constants defining the gaps between attempts to lock a file. The
 * first backoff period is approximately INITIAL_BACKOFF_MS
 * milliseconds. The longest backoff period is approximately
 * (BACKOFF_MAX_MULTIPLIER * INITIAL_BACKOFF_MS) milliseconds.
 */
#define INITIAL_BACKOFF_MS 1L
#define BACKOFF_MAX_MULTIPLIER 1000

/*
 * Try locking path, retrying with quadratic backoff for at least
 * timeout_ms milliseconds. If timeout_ms is 0, try locking the file
 * exactly once. If timeout_ms is -1, try indefinitely.
 */
static int lock_file_timeout(struct lock_file *lk, const char *path,
			     int flags, long timeout_ms)
{
	int n = 1;
	int multiplier = 1;
	long remaining_us = 0;
	static int random_initialized = 0;

	if (timeout_ms == 0)
		return lock_file(lk, path, flags);

	if (!random_initialized) {
		srandom((unsigned int)getpid());
		random_initialized = 1;
	}

	if (timeout_ms > 0) {
		/* avoid overflow */
		if (timeout_ms <= LONG_MAX / 1000)
			remaining_us = timeout_ms * 1000;
		else
			remaining_us = LONG_MAX;
	}

	while (1) {
		long backoff_ms, wait_us;
		int fd;

		fd = lock_file(lk, path, flags);

		if (fd >= 0)
			return fd; /* success */
		else if (errno != EEXIST)
			return -1; /* failure other than lock held */
		else if (timeout_ms > 0 && remaining_us <= 0)
			return -1; /* failure due to timeout */

		backoff_ms = multiplier * INITIAL_BACKOFF_MS;
		/* back off for between 0.75*backoff_ms and 1.25*backoff_ms */
		wait_us = (750 + random() % 500) * backoff_ms;
		sleep_microseconds(wait_us);
		remaining_us -= wait_us;

		/* Recursion: (n+1)^2 = n^2 + 2n + 1 */
		multiplier += 2*n + 1;
		if (multiplier > BACKOFF_MAX_MULTIPLIER)
			multiplier = BACKOFF_MAX_MULTIPLIER;
		else
			n++;
	}
}

void unable_to_lock_message(const char *path, int err, struct strbuf *buf)
{
	if (err == EEXIST) {
		strbuf_addf(buf, "Unable to create '%s.lock': %s.\n\n"
		    "If no other git process is currently running, this probably means a\n"
		    "git process crashed in this repository earlier. Make sure no other git\n"
		    "process is running and remove the file manually to continue.",
			    absolute_path(path), strerror(err));
	} else
		strbuf_addf(buf, "Unable to create '%s.lock': %s",
			    absolute_path(path), strerror(err));
}

NORETURN void unable_to_lock_die(const char *path, int err)
{
	struct strbuf buf = STRBUF_INIT;

	unable_to_lock_message(path, err, &buf);
	die("%s", buf.buf);
}

/* This should return a meaningful errno on failure */
int hold_lock_file_for_update_timeout(struct lock_file *lk, const char *path,
				      int flags, long timeout_ms)
{
	int fd = lock_file_timeout(lk, path, flags, timeout_ms);
	if (fd < 0 && (flags & LOCK_DIE_ON_ERROR))
		unable_to_lock_die(path, errno);
	return fd;
}

int hold_lock_file_for_append(struct lock_file *lk, const char *path, int flags)
{
	int fd, orig_fd;

	fd = lock_file(lk, path, flags);
	if (fd < 0) {
		if (flags & LOCK_DIE_ON_ERROR)
			unable_to_lock_die(path, errno);
		return fd;
	}

	orig_fd = open(path, O_RDONLY);
	if (orig_fd < 0) {
		if (errno != ENOENT) {
			int save_errno = errno;

			if (flags & LOCK_DIE_ON_ERROR)
				die("cannot open '%s' for copying", path);
			rollback_lock_file(lk);
			error("cannot open '%s' for copying", path);
			errno = save_errno;
			return -1;
		}
	} else if (copy_fd(orig_fd, fd)) {
		int save_errno = errno;

		if (flags & LOCK_DIE_ON_ERROR)
			die("failed to prepare '%s' for appending", path);
		close(orig_fd);
		rollback_lock_file(lk);
		errno = save_errno;
		return -1;
	} else {
		close(orig_fd);
	}
	return fd;
}

FILE *fdopen_lock_file(struct lock_file *lk, const char *mode)
{
	if (!lk->active)
		die("BUG: fdopen_lock_file() called for unlocked object");
	if (lk->fp)
		die("BUG: fdopen_lock_file() called twice for file '%s'", lk->filename.buf);

	lk->fp = fdopen(lk->fd, mode);
	return lk->fp;
}

int get_lock_file_fd(struct lock_file *lk)
{
	if (!lk->active)
		die("BUG: get_lock_file_fd() called for unlocked object");
	return lk->fd;
}

FILE *get_lock_file_fp(struct lock_file *lk)
{
	if (!lk->active)
		die("BUG: get_lock_file_fp() called for unlocked object");
	return lk->fp;
}

char *get_locked_file_path(struct lock_file *lk)
{
	if (!lk->active)
		die("BUG: get_locked_file_path() called for unlocked object");
	if (lk->filename.len <= LOCK_SUFFIX_LEN)
		die("BUG: get_locked_file_path() called for malformed lock object");
	return xmemdupz(lk->filename.buf, lk->filename.len - LOCK_SUFFIX_LEN);
}

int close_lock_file(struct lock_file *lk)
{
	int fd = lk->fd;
	FILE *fp = lk->fp;
	int err;

	if (fd < 0)
		return 0;

	lk->fd = -1;
	if (fp) {
		lk->fp = NULL;

		/*
		 * Note: no short-circuiting here; we want to fclose()
		 * in any case!
		 */
		err = ferror(fp) | fclose(fp);
	} else {
		err = close(fd);
	}

	if (err) {
		int save_errno = errno;
		rollback_lock_file(lk);
		errno = save_errno;
		return -1;
	}

	return 0;
}

int reopen_lock_file(struct lock_file *lk)
{
	if (0 <= lk->fd)
		die(_("BUG: reopen a lockfile that is still open"));
	if (!lk->active)
		die(_("BUG: reopen a lockfile that has been committed"));
	lk->fd = open(lk->filename.buf, O_WRONLY);
	return lk->fd;
}

int commit_lock_file_to(struct lock_file *lk, const char *path)
{
	if (!lk->active)
		die("BUG: attempt to commit unlocked object to \"%s\"", path);

	if (close_lock_file(lk))
		return -1;

	if (rename(lk->filename.buf, path)) {
		int save_errno = errno;
		rollback_lock_file(lk);
		errno = save_errno;
		return -1;
	}

	lk->active = 0;
	strbuf_reset(&lk->filename);
	return 0;
}

int commit_lock_file(struct lock_file *lk)
{
	static struct strbuf result_file = STRBUF_INIT;
	int err;

	if (!lk->active)
		die("BUG: attempt to commit unlocked object");

	if (lk->filename.len <= LOCK_SUFFIX_LEN ||
	    strcmp(lk->filename.buf + lk->filename.len - LOCK_SUFFIX_LEN, LOCK_SUFFIX))
		die("BUG: lockfile filename corrupt");

	/* remove ".lock": */
	strbuf_add(&result_file, lk->filename.buf,
		   lk->filename.len - LOCK_SUFFIX_LEN);
	err = commit_lock_file_to(lk, result_file.buf);
	strbuf_reset(&result_file);
	return err;
}

void rollback_lock_file(struct lock_file *lk)
{
	if (!lk->active)
		return;

	if (!close_lock_file(lk)) {
		unlink_or_warn(lk->filename.buf);
		lk->active = 0;
		strbuf_reset(&lk->filename);
	}
}
