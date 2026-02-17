/*
 * Copyright (c) 2005, Junio C Hamano
 */

#include "git-compat-util.h"
#include "abspath.h"
#include "gettext.h"
#include "lockfile.h"
#include "parse.h"
#include "strbuf.h"
#include "wrapper.h"

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
	while (i && is_dir_sep(path->buf[i - 1]))
		i--;

	/*
	 * then go backwards until a slash, or the beginning of the
	 * string
	 */
	while (i && !is_dir_sep(path->buf[i - 1]))
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

/*
 * Lock PID file functions - write PID to a foo~pid.lock file alongside
 * the lock file for debugging stale locks. The PID file is registered
 * as a tempfile so it gets cleaned up by signal/atexit handlers.
 *
 * Naming: For "foo.lock", the PID file is "foo~pid.lock". The tilde is
 * forbidden in refnames and allowed in Windows filenames, guaranteeing
 * no collision with the refs namespace.
 */

/* Global config variable, initialized from core.lockfilePid */
int lockfile_pid_enabled;

/*
 * Path generation helpers.
 * Given base path "foo", generate:
 *   - lock path: "foo.lock"
 *   - pid path:  "foo-pid.lock"
 */
static void get_lock_path(struct strbuf *out, const char *path)
{
	strbuf_addstr(out, path);
	strbuf_addstr(out, LOCK_SUFFIX);
}

static void get_pid_path(struct strbuf *out, const char *path)
{
	strbuf_addstr(out, path);
	strbuf_addstr(out, LOCK_PID_INFIX);
	strbuf_addstr(out, LOCK_SUFFIX);
}

static struct tempfile *create_lock_pid_file(const char *pid_path, int mode)
{
	struct strbuf content = STRBUF_INIT;
	struct tempfile *pid_tempfile = NULL;
	int fd;

	if (!lockfile_pid_enabled)
		goto out;

	fd = open(pid_path, O_WRONLY | O_CREAT | O_EXCL, mode);
	if (fd < 0)
		goto out;

	strbuf_addf(&content, "pid %" PRIuMAX "\n", (uintmax_t)getpid());
	if (write_in_full(fd, content.buf, content.len) < 0) {
		warning_errno(_("could not write lock pid file '%s'"), pid_path);
		close(fd);
		unlink(pid_path);
		goto out;
	}

	close(fd);
	pid_tempfile = register_tempfile(pid_path);

out:
	strbuf_release(&content);
	return pid_tempfile;
}

static int read_lock_pid(const char *pid_path, uintmax_t *pid_out)
{
	struct strbuf content = STRBUF_INIT;
	const char *val;
	int ret = -1;

	if (strbuf_read_file(&content, pid_path, LOCK_PID_MAXLEN) <= 0)
		goto out;

	strbuf_rtrim(&content);

	if (skip_prefix(content.buf, "pid ", &val)) {
		char *endptr;
		*pid_out = strtoumax(val, &endptr, 10);
		if (*pid_out > 0 && !*endptr)
			ret = 0;
	}

	if (ret)
		warning(_("malformed lock pid file '%s'"), pid_path);

out:
	strbuf_release(&content);
	return ret;
}

/* Make sure errno contains a meaningful value on error */
static int lock_file(struct lock_file *lk, const char *path, int flags,
		     int mode)
{
	struct strbuf base_path = STRBUF_INIT;
	struct strbuf lock_path = STRBUF_INIT;
	struct strbuf pid_path = STRBUF_INIT;

	strbuf_addstr(&base_path, path);
	if (!(flags & LOCK_NO_DEREF))
		resolve_symlink(&base_path);

	get_lock_path(&lock_path, base_path.buf);
	get_pid_path(&pid_path, base_path.buf);

	lk->tempfile = create_tempfile_mode(lock_path.buf, mode);
	if (lk->tempfile)
		lk->pid_tempfile = create_lock_pid_file(pid_path.buf, mode);

	strbuf_release(&base_path);
	strbuf_release(&lock_path);
	strbuf_release(&pid_path);
	return lk->tempfile ? lk->tempfile->fd : -1;
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
			     int flags, long timeout_ms, int mode)
{
	int n = 1;
	int multiplier = 1;
	long remaining_ms = 0;
	static int random_initialized = 0;

	if (timeout_ms == 0)
		return lock_file(lk, path, flags, mode);

	if (!random_initialized) {
		srand((unsigned int)getpid());
		random_initialized = 1;
	}

	if (timeout_ms > 0)
		remaining_ms = timeout_ms;

	while (1) {
		long backoff_ms, wait_ms;
		int fd;

		fd = lock_file(lk, path, flags, mode);

		if (fd >= 0)
			return fd; /* success */
		else if (errno != EEXIST)
			return -1; /* failure other than lock held */
		else if (timeout_ms > 0 && remaining_ms <= 0)
			return -1; /* failure due to timeout */

		backoff_ms = multiplier * INITIAL_BACKOFF_MS;
		/* back off for between 0.75*backoff_ms and 1.25*backoff_ms */
		wait_ms = (750 + rand() % 500) * backoff_ms / 1000;
		sleep_millisec(wait_ms);
		remaining_ms -= wait_ms;

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
		const char *abs_path = absolute_path(path);
		struct strbuf lock_path = STRBUF_INIT;
		struct strbuf pid_path = STRBUF_INIT;
		uintmax_t pid;
		int pid_status = 0; /* 0 = unknown, 1 = running, -1 = stale */

		get_lock_path(&lock_path, abs_path);
		get_pid_path(&pid_path, abs_path);

		strbuf_addf(buf, _("Unable to create '%s': %s.\n\n"),
			    lock_path.buf, strerror(err));

		/*
		 * Try to read PID file unconditionally - it may exist if
		 * core.lockfilePid was enabled.
		 */
		if (!read_lock_pid(pid_path.buf, &pid)) {
			if (kill((pid_t)pid, 0) == 0 || errno == EPERM)
				pid_status = 1;  /* running (or no permission to signal) */
			else if (errno == ESRCH)
				pid_status = -1; /* no such process - stale lock */
		}

		if (pid_status == 1)
			strbuf_addf(buf, _("Lock may be held by process %" PRIuMAX "; "
					   "if no git process is running, the lock file "
					   "may be stale (PIDs can be reused)"),
				    pid);
		else if (pid_status == -1)
			strbuf_addf(buf, _("Lock was held by process %" PRIuMAX ", "
					   "which is no longer running; the lock file "
					   "appears to be stale"),
				    pid);
		else
			strbuf_addstr(buf, _("Another git process seems to be running in this repository, "
					     "or the lock file may be stale"));

		strbuf_release(&lock_path);
		strbuf_release(&pid_path);
	} else {
		strbuf_addf(buf, _("Unable to create '%s.lock': %s"),
			    absolute_path(path), strerror(err));
	}
}

NORETURN void unable_to_lock_die(const char *path, int err)
{
	struct strbuf buf = STRBUF_INIT;

	unable_to_lock_message(path, err, &buf);
	die("%s", buf.buf);
}

/* This should return a meaningful errno on failure */
int hold_lock_file_for_update_timeout_mode(struct lock_file *lk,
					   const char *path, int flags,
					   long timeout_ms, int mode)
{
	int fd = lock_file_timeout(lk, path, flags, timeout_ms, mode);
	if (fd < 0) {
		if (flags & LOCK_DIE_ON_ERROR)
			unable_to_lock_die(path, errno);
		if (flags & LOCK_REPORT_ON_ERROR) {
			struct strbuf buf = STRBUF_INIT;
			unable_to_lock_message(path, errno, &buf);
			error("%s", buf.buf);
			strbuf_release(&buf);
		}
	}
	return fd;
}

char *get_locked_file_path(struct lock_file *lk)
{
	struct strbuf ret = STRBUF_INIT;

	strbuf_addstr(&ret, get_tempfile_path(lk->tempfile));
	if (ret.len <= LOCK_SUFFIX_LEN ||
	    strcmp(ret.buf + ret.len - LOCK_SUFFIX_LEN, LOCK_SUFFIX))
		BUG("get_locked_file_path() called for malformed lock object");
	/* remove ".lock": */
	strbuf_setlen(&ret, ret.len - LOCK_SUFFIX_LEN);
	return strbuf_detach(&ret, NULL);
}

int commit_lock_file(struct lock_file *lk)
{
	char *result_path = get_locked_file_path(lk);

	delete_tempfile(&lk->pid_tempfile);

	if (commit_lock_file_to(lk, result_path)) {
		int save_errno = errno;
		free(result_path);
		errno = save_errno;
		return -1;
	}
	free(result_path);
	return 0;
}

int rollback_lock_file(struct lock_file *lk)
{
	delete_tempfile(&lk->pid_tempfile);
	return delete_tempfile(&lk->tempfile);
}
