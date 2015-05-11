/*
 * Copyright (c) 2005, Junio C Hamano
 */

#include "cache.h"
#include "lockfile.h"

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

/* Make sure errno contains a meaningful value on error */
static int lock_file(struct lock_file *lk, const char *path, int flags,
		     int mode)
{
	struct strbuf filename = STRBUF_INIT;

	strbuf_addstr(&filename, path);
	if (!(flags & LOCK_NO_DEREF))
		resolve_symlink(&filename);

	strbuf_addstr(&filename, LOCK_SUFFIX);
	lk->tempfile = create_tempfile_mode(filename.buf, mode);
	strbuf_release(&filename);
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
		strbuf_addf(buf, _("Unable to create '%s.lock': %s.\n\n"
		    "Another git process seems to be running in this repository, e.g.\n"
		    "an editor opened by 'git commit'. Please make sure all processes\n"
		    "are terminated then try again. If it still fails, a git process\n"
		    "may have crashed in this repository earlier:\n"
		    "remove the file manually to continue."),
			    absolute_path(path), strerror(err));
	} else
		strbuf_addf(buf, _("Unable to create '%s.lock': %s"),
			    absolute_path(path), strerror(err));
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

	if (commit_lock_file_to(lk, result_path)) {
		int save_errno = errno;
		free(result_path);
		errno = save_errno;
		return -1;
	}
	free(result_path);
	return 0;
}
