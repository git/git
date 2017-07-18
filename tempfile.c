/*
 * State diagram and cleanup
 * -------------------------
 *
 * If the program exits while a temporary file is active, we want to
 * make sure that we remove it. This is done by remembering the active
 * temporary files in a linked list, `tempfile_list`. An `atexit(3)`
 * handler and a signal handler are registered, to clean up any active
 * temporary files.
 *
 * Because the signal handler can run at any time, `tempfile_list` and
 * the `tempfile` objects that comprise it must be kept in
 * self-consistent states at all times.
 *
 * The possible states of a `tempfile` object are as follows:
 *
 * - Uninitialized. In this state the object's `on_list` field must be
 *   zero but the rest of its contents need not be initialized. As
 *   soon as the object is used in any way, it is irrevocably
 *   registered in `tempfile_list`, and `on_list` is set.
 *
 * - Active, file open (after `create_tempfile()` or
 *   `reopen_tempfile()`). In this state:
 *
 *   - the temporary file exists
 *   - `active` is set
 *   - `filename` holds the filename of the temporary file
 *   - `fd` holds a file descriptor open for writing to it
 *   - `fp` holds a pointer to an open `FILE` object if and only if
 *     `fdopen_tempfile()` has been called on the object
 *   - `owner` holds the PID of the process that created the file
 *
 * - Active, file closed (after successful `close_tempfile()`). Same
 *   as the previous state, except that the temporary file is closed,
 *   `fd` is -1, and `fp` is `NULL`.
 *
 * - Inactive (after `delete_tempfile()`, `rename_tempfile()`, a
 *   failed attempt to create a temporary file, or a failed
 *   `close_tempfile()`). In this state:
 *
 *   - `active` is unset
 *   - `filename` is empty (usually, though there are transitory
 *     states in which this condition doesn't hold). Client code should
 *     *not* rely on the filename being empty in this state.
 *   - `fd` is -1 and `fp` is `NULL`
 *   - the object is left registered in the `tempfile_list`, and
 *     `on_list` is set.
 *
 * A temporary file is owned by the process that created it. The
 * `tempfile` has an `owner` field that records the owner's PID. This
 * field is used to prevent a forked process from deleting a temporary
 * file created by its parent.
 */

#include "cache.h"
#include "tempfile.h"
#include "sigchain.h"

static struct tempfile *volatile tempfile_list;

static void remove_tempfiles(int skip_fclose)
{
	pid_t me = getpid();

	while (tempfile_list) {
		if (tempfile_list->owner == me) {
			/* fclose() is not safe to call in a signal handler */
			if (skip_fclose)
				tempfile_list->fp = NULL;
			delete_tempfile(tempfile_list);
		}
		tempfile_list = tempfile_list->next;
	}
}

static void remove_tempfiles_on_exit(void)
{
	remove_tempfiles(0);
}

static void remove_tempfiles_on_signal(int signo)
{
	remove_tempfiles(1);
	sigchain_pop(signo);
	raise(signo);
}

/*
 * Initialize *tempfile if necessary and add it to tempfile_list.
 */
static void prepare_tempfile_object(struct tempfile *tempfile)
{
	if (!tempfile_list) {
		/* One-time initialization */
		sigchain_push_common(remove_tempfiles_on_signal);
		atexit(remove_tempfiles_on_exit);
	}

	if (tempfile->active)
		die("BUG: prepare_tempfile_object called for active object");
	if (!tempfile->on_list) {
		/* Initialize *tempfile and add it to tempfile_list: */
		tempfile->fd = -1;
		tempfile->fp = NULL;
		tempfile->active = 0;
		tempfile->owner = 0;
		strbuf_init(&tempfile->filename, 0);
		tempfile->next = tempfile_list;
		tempfile_list = tempfile;
		tempfile->on_list = 1;
	} else if (tempfile->filename.len) {
		/* This shouldn't happen, but better safe than sorry. */
		die("BUG: prepare_tempfile_object called for improperly-reset object");
	}
}

/* Make sure errno contains a meaningful value on error */
int create_tempfile(struct tempfile *tempfile, const char *path)
{
	prepare_tempfile_object(tempfile);

	strbuf_add_absolute_path(&tempfile->filename, path);
	tempfile->fd = open(tempfile->filename.buf, O_RDWR | O_CREAT | O_EXCL, 0666);
	if (tempfile->fd < 0) {
		strbuf_reset(&tempfile->filename);
		return -1;
	}
	tempfile->owner = getpid();
	tempfile->active = 1;
	if (adjust_shared_perm(tempfile->filename.buf)) {
		int save_errno = errno;
		error("cannot fix permission bits on %s", tempfile->filename.buf);
		delete_tempfile(tempfile);
		errno = save_errno;
		return -1;
	}
	return tempfile->fd;
}

void register_tempfile(struct tempfile *tempfile, const char *path)
{
	prepare_tempfile_object(tempfile);
	strbuf_add_absolute_path(&tempfile->filename, path);
	tempfile->owner = getpid();
	tempfile->active = 1;
}

int mks_tempfile_sm(struct tempfile *tempfile,
		    const char *template, int suffixlen, int mode)
{
	prepare_tempfile_object(tempfile);

	strbuf_add_absolute_path(&tempfile->filename, template);
	tempfile->fd = git_mkstemps_mode(tempfile->filename.buf, suffixlen, mode);
	if (tempfile->fd < 0) {
		strbuf_reset(&tempfile->filename);
		return -1;
	}
	tempfile->owner = getpid();
	tempfile->active = 1;
	return tempfile->fd;
}

int mks_tempfile_tsm(struct tempfile *tempfile,
		     const char *template, int suffixlen, int mode)
{
	const char *tmpdir;

	prepare_tempfile_object(tempfile);

	tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = "/tmp";

	strbuf_addf(&tempfile->filename, "%s/%s", tmpdir, template);
	tempfile->fd = git_mkstemps_mode(tempfile->filename.buf, suffixlen, mode);
	if (tempfile->fd < 0) {
		strbuf_reset(&tempfile->filename);
		return -1;
	}
	tempfile->owner = getpid();
	tempfile->active = 1;
	return tempfile->fd;
}

int xmks_tempfile_m(struct tempfile *tempfile, const char *template, int mode)
{
	int fd;
	struct strbuf full_template = STRBUF_INIT;

	strbuf_add_absolute_path(&full_template, template);
	fd = mks_tempfile_m(tempfile, full_template.buf, mode);
	if (fd < 0)
		die_errno("Unable to create temporary file '%s'",
			  full_template.buf);

	strbuf_release(&full_template);
	return fd;
}

FILE *fdopen_tempfile(struct tempfile *tempfile, const char *mode)
{
	if (!tempfile->active)
		die("BUG: fdopen_tempfile() called for inactive object");
	if (tempfile->fp)
		die("BUG: fdopen_tempfile() called for open object");

	tempfile->fp = fdopen(tempfile->fd, mode);
	return tempfile->fp;
}

const char *get_tempfile_path(struct tempfile *tempfile)
{
	if (!tempfile->active)
		die("BUG: get_tempfile_path() called for inactive object");
	return tempfile->filename.buf;
}

int get_tempfile_fd(struct tempfile *tempfile)
{
	if (!tempfile->active)
		die("BUG: get_tempfile_fd() called for inactive object");
	return tempfile->fd;
}

FILE *get_tempfile_fp(struct tempfile *tempfile)
{
	if (!tempfile->active)
		die("BUG: get_tempfile_fp() called for inactive object");
	return tempfile->fp;
}

int close_tempfile(struct tempfile *tempfile)
{
	int fd = tempfile->fd;
	FILE *fp = tempfile->fp;
	int err;

	if (fd < 0)
		return 0;

	tempfile->fd = -1;
	if (fp) {
		tempfile->fp = NULL;

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
		delete_tempfile(tempfile);
		errno = save_errno;
		return -1;
	}

	return 0;
}

int reopen_tempfile(struct tempfile *tempfile)
{
	if (0 <= tempfile->fd)
		die("BUG: reopen_tempfile called for an open object");
	if (!tempfile->active)
		die("BUG: reopen_tempfile called for an inactive object");
	tempfile->fd = open(tempfile->filename.buf, O_WRONLY);
	return tempfile->fd;
}

int rename_tempfile(struct tempfile *tempfile, const char *path)
{
	if (!tempfile->active)
		die("BUG: rename_tempfile called for inactive object");

	if (close_tempfile(tempfile))
		return -1;

	if (rename(tempfile->filename.buf, path)) {
		int save_errno = errno;
		delete_tempfile(tempfile);
		errno = save_errno;
		return -1;
	}

	tempfile->active = 0;
	strbuf_reset(&tempfile->filename);
	return 0;
}

void delete_tempfile(struct tempfile *tempfile)
{
	if (!tempfile->active)
		return;

	if (!close_tempfile(tempfile)) {
		unlink_or_warn(tempfile->filename.buf);
		tempfile->active = 0;
		strbuf_reset(&tempfile->filename);
	}
}
