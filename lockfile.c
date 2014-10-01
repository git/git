/*
 * Copyright (c) 2005, Junio C Hamano
 */
#include "cache.h"
#include "sigchain.h"

/*
 * File write-locks as used by Git.
 *
 * For an overview of how to use the lockfile API, please see
 *
 *     Documentation/technical/api-lockfile.txt
 *
 * This module keeps track of all locked files in lock_file_list for
 * use at cleanup. This list and the lock_file objects that comprise
 * it must be kept in self-consistent states at all time, because the
 * program can be interrupted any time by a signal, in which case the
 * signal handler will walk through the list attempting to clean up
 * any open lock files.
 *
 * A lockfile is owned by the process that created it. The lock_file
 * object has an "owner" field that records its owner. This field is
 * used to prevent a forked process from closing a lockfile created by
 * its parent.
 *
 * The possible states of a lock_file object are as follows:
 *
 * - Uninitialized.  In this state the object's on_list field must be
 *   zero but the rest of its contents need not be initialized.  As
 *   soon as the object is used in any way, it is irrevocably
 *   registered in the lock_file_list, and on_list is set.
 *
 * - Locked, lockfile open (after hold_lock_file_for_update(),
 *   hold_lock_file_for_append(), or reopen_lock_file()). In this
 *   state:
 *   - the lockfile exists
 *   - active is set
 *   - filename holds the filename of the lockfile
 *   - fd holds a file descriptor open for writing to the lockfile
 *   - owner holds the PID of the process that locked the file
 *
 * - Locked, lockfile closed (after successful close_lock_file()).
 *   Same as the previous state, except that the lockfile is closed
 *   and fd is -1.
 *
 * - Unlocked (after commit_lock_file(), rollback_lock_file(), a
 *   failed attempt to lock, or a failed close_lock_file()).  In this
 *   state:
 *   - active is unset
 *   - filename is empty (usually, though there are transitory
 *     states in which this condition doesn't hold). Client code should
 *     *not* rely on the filename being empty in this state.
 *   - fd is -1
 *   - the object is left registered in the lock_file_list, and
 *     on_list is set.
 */

static struct lock_file *volatile lock_file_list;

static void remove_lock_file(void)
{
	pid_t me = getpid();

	while (lock_file_list) {
		if (lock_file_list->owner == me)
			rollback_lock_file(lock_file_list);
		lock_file_list = lock_file_list->next;
	}
}

static void remove_lock_file_on_signal(int signo)
{
	remove_lock_file();
	sigchain_pop(signo);
	raise(signo);
}

/*
 * p = absolute or relative path name
 *
 * Return a pointer into p showing the beginning of the last path name
 * element.  If p is empty or the root directory ("/"), just return p.
 */
static char *last_path_elm(char *p)
{
	/* r starts pointing to null at the end of the string */
	char *r = strchr(p, '\0');

	if (r == p)
		return p; /* just return empty string */

	r--; /* back up to last non-null character */

	/* back up past trailing slashes, if any */
	while (r > p && *r == '/')
		r--;

	/*
	 * then go backwards until I hit a slash, or the beginning of
	 * the string
	 */
	while (r > p && *(r-1) != '/')
		r--;
	return r;
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
		else {
			/*
			 * link is a relative path, so replace the
			 * last element of p with it.
			 */
			char *r = last_path_elm(path->buf);
			strbuf_setlen(path, r - path->buf);
		}

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
		sigchain_push_common(remove_lock_file_on_signal);
		atexit(remove_lock_file);
	}

	if (lk->active)
		die("BUG: cannot lock_file(\"%s\") using active struct lock_file",
		    path);
	if (!lk->on_list) {
		/* Initialize *lk and add it to lock_file_list: */
		lk->fd = -1;
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

	strbuf_add(&lk->filename, path, pathlen);
	if (!(flags & LOCK_NODEREF))
		resolve_symlink(&lk->filename);
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

int unable_to_lock_error(const char *path, int err)
{
	struct strbuf buf = STRBUF_INIT;

	unable_to_lock_message(path, err, &buf);
	error("%s", buf.buf);
	strbuf_release(&buf);
	return -1;
}

NORETURN void unable_to_lock_die(const char *path, int err)
{
	struct strbuf buf = STRBUF_INIT;

	unable_to_lock_message(path, err, &buf);
	die("%s", buf.buf);
}

/* This should return a meaningful errno on failure */
int hold_lock_file_for_update(struct lock_file *lk, const char *path, int flags)
{
	int fd = lock_file(lk, path, flags);
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
			if (flags & LOCK_DIE_ON_ERROR)
				die("cannot open '%s' for copying", path);
			rollback_lock_file(lk);
			return error("cannot open '%s' for copying", path);
		}
	} else if (copy_fd(orig_fd, fd)) {
		if (flags & LOCK_DIE_ON_ERROR)
			exit(128);
		rollback_lock_file(lk);
		return -1;
	}
	return fd;
}

int close_lock_file(struct lock_file *lk)
{
	int fd = lk->fd;

	if (fd < 0)
		return 0;

	lk->fd = -1;
	if (close(fd)) {
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

int commit_lock_file(struct lock_file *lk)
{
	static struct strbuf result_file = STRBUF_INIT;
	int err;

	if (!lk->active)
		die("BUG: attempt to commit unlocked object");

	if (close_lock_file(lk))
		return -1;

	/* remove ".lock": */
	strbuf_add(&result_file, lk->filename.buf,
		   lk->filename.len - LOCK_SUFFIX_LEN);
	err = rename(lk->filename.buf, result_file.buf);
	strbuf_reset(&result_file);
	if (err) {
		int save_errno = errno;
		rollback_lock_file(lk);
		errno = save_errno;
		return -1;
	}

	lk->active = 0;
	strbuf_reset(&lk->filename);
	return 0;
}

int hold_locked_index(struct lock_file *lk, int die_on_error)
{
	return hold_lock_file_for_update(lk, get_index_file(),
					 die_on_error
					 ? LOCK_DIE_ON_ERROR
					 : 0);
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
