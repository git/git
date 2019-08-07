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
 * - Active, file closed (after `close_tempfile_gently()`). Same
 *   as the previous state, except that the temporary file is closed,
 *   `fd` is -1, and `fp` is `NULL`.
 *
 * - Inactive (after `delete_tempfile()`, `rename_tempfile()`, or a
 *   failed attempt to create a temporary file). In this state:
 *
 *   - `active` is unset
 *   - `filename` is empty (usually, though there are transitory
 *     states in which this condition doesn't hold). Client code should
 *     *not* rely on the filename being empty in this state.
 *   - `fd` is -1 and `fp` is `NULL`
 *   - the object is removed from `tempfile_list` (but could be used again)
 *
 * A temporary file is owned by the process that created it. The
 * `tempfile` has an `owner` field that records the owner's PID. This
 * field is used to prevent a forked process from deleting a temporary
 * file created by its parent.
 */

#include "cache.h"
#include "tempfile.h"
#include "sigchain.h"

static VOLATILE_LIST_HEAD(tempfile_list);

static void remove_tempfiles(int in_signal_handler)
{
	pid_t me = getpid();
	volatile struct volatile_list_head *pos;

	list_for_each(pos, &tempfile_list) {
		struct tempfile *p = list_entry(pos, struct tempfile, list);

		if (!is_tempfile_active(p) || p->owner != me)
			continue;

		if (p->fd >= 0)
			close(p->fd);

		if (in_signal_handler)
			unlink(p->filename.buf);
		else
			unlink_or_warn(p->filename.buf);

		p->active = 0;
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

static struct tempfile *new_tempfile(void)
{
	struct tempfile *tempfile = xmalloc(sizeof(*tempfile));
	tempfile->fd = -1;
	tempfile->fp = NULL;
	tempfile->active = 0;
	tempfile->owner = 0;
	INIT_LIST_HEAD(&tempfile->list);
	strbuf_init(&tempfile->filename, 0);
	return tempfile;
}

static void activate_tempfile(struct tempfile *tempfile)
{
	static int initialized;

	if (is_tempfile_active(tempfile))
		BUG("activate_tempfile called for active object");

	if (!initialized) {
		sigchain_push_common(remove_tempfiles_on_signal);
		atexit(remove_tempfiles_on_exit);
		initialized = 1;
	}

	volatile_list_add(&tempfile->list, &tempfile_list);
	tempfile->owner = getpid();
	tempfile->active = 1;
}

static void deactivate_tempfile(struct tempfile *tempfile)
{
	tempfile->active = 0;
	strbuf_release(&tempfile->filename);
	volatile_list_del(&tempfile->list);
	free(tempfile);
}

/* Make sure errno contains a meaningful value on error */
struct tempfile *create_tempfile(const char *path)
{
	struct tempfile *tempfile = new_tempfile();

	strbuf_add_absolute_path(&tempfile->filename, path);
	tempfile->fd = open(tempfile->filename.buf,
			    O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
	if (O_CLOEXEC && tempfile->fd < 0 && errno == EINVAL)
		/* Try again w/o O_CLOEXEC: the kernel might not support it */
		tempfile->fd = open(tempfile->filename.buf,
				    O_RDWR | O_CREAT | O_EXCL, 0666);
	if (tempfile->fd < 0) {
		deactivate_tempfile(tempfile);
		return NULL;
	}
	activate_tempfile(tempfile);
	if (adjust_shared_perm(tempfile->filename.buf)) {
		int save_errno = errno;
		error("cannot fix permission bits on %s", tempfile->filename.buf);
		delete_tempfile(&tempfile);
		errno = save_errno;
		return NULL;
	}

	return tempfile;
}

struct tempfile *register_tempfile(const char *path)
{
	struct tempfile *tempfile = new_tempfile();
	strbuf_add_absolute_path(&tempfile->filename, path);
	activate_tempfile(tempfile);
	return tempfile;
}

struct tempfile *mks_tempfile_sm(const char *filename_template, int suffixlen, int mode)
{
	struct tempfile *tempfile = new_tempfile();

	strbuf_add_absolute_path(&tempfile->filename, filename_template);
	tempfile->fd = git_mkstemps_mode(tempfile->filename.buf, suffixlen, mode);
	if (tempfile->fd < 0) {
		deactivate_tempfile(tempfile);
		return NULL;
	}
	activate_tempfile(tempfile);
	return tempfile;
}

struct tempfile *mks_tempfile_tsm(const char *filename_template, int suffixlen, int mode)
{
	struct tempfile *tempfile = new_tempfile();
	const char *tmpdir;

	tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = "/tmp";

	strbuf_addf(&tempfile->filename, "%s/%s", tmpdir, filename_template);
	tempfile->fd = git_mkstemps_mode(tempfile->filename.buf, suffixlen, mode);
	if (tempfile->fd < 0) {
		deactivate_tempfile(tempfile);
		return NULL;
	}
	activate_tempfile(tempfile);
	return tempfile;
}

struct tempfile *xmks_tempfile_m(const char *filename_template, int mode)
{
	struct tempfile *tempfile;
	struct strbuf full_template = STRBUF_INIT;

	strbuf_add_absolute_path(&full_template, filename_template);
	tempfile = mks_tempfile_m(full_template.buf, mode);
	if (!tempfile)
		die_errno("Unable to create temporary file '%s'",
			  full_template.buf);

	strbuf_release(&full_template);
	return tempfile;
}

FILE *fdopen_tempfile(struct tempfile *tempfile, const char *mode)
{
	if (!is_tempfile_active(tempfile))
		BUG("fdopen_tempfile() called for inactive object");
	if (tempfile->fp)
		BUG("fdopen_tempfile() called for open object");

	tempfile->fp = fdopen(tempfile->fd, mode);
	return tempfile->fp;
}

const char *get_tempfile_path(struct tempfile *tempfile)
{
	if (!is_tempfile_active(tempfile))
		BUG("get_tempfile_path() called for inactive object");
	return tempfile->filename.buf;
}

int get_tempfile_fd(struct tempfile *tempfile)
{
	if (!is_tempfile_active(tempfile))
		BUG("get_tempfile_fd() called for inactive object");
	return tempfile->fd;
}

FILE *get_tempfile_fp(struct tempfile *tempfile)
{
	if (!is_tempfile_active(tempfile))
		BUG("get_tempfile_fp() called for inactive object");
	return tempfile->fp;
}

int close_tempfile_gently(struct tempfile *tempfile)
{
	int fd;
	FILE *fp;
	int err;

	if (!is_tempfile_active(tempfile) || tempfile->fd < 0)
		return 0;

	fd = tempfile->fd;
	fp = tempfile->fp;
	tempfile->fd = -1;
	if (fp) {
		tempfile->fp = NULL;
		if (ferror(fp)) {
			err = -1;
			if (!fclose(fp))
				errno = EIO;
		} else {
			err = fclose(fp);
		}
	} else {
		err = close(fd);
	}

	return err ? -1 : 0;
}

int reopen_tempfile(struct tempfile *tempfile)
{
	if (!is_tempfile_active(tempfile))
		BUG("reopen_tempfile called for an inactive object");
	if (0 <= tempfile->fd)
		BUG("reopen_tempfile called for an open object");
	tempfile->fd = open(tempfile->filename.buf, O_WRONLY|O_TRUNC);
	return tempfile->fd;
}

int rename_tempfile(struct tempfile **tempfile_p, const char *path)
{
	struct tempfile *tempfile = *tempfile_p;

	if (!is_tempfile_active(tempfile))
		BUG("rename_tempfile called for inactive object");

	if (close_tempfile_gently(tempfile)) {
		delete_tempfile(tempfile_p);
		return -1;
	}

	if (rename(tempfile->filename.buf, path)) {
		int save_errno = errno;
		delete_tempfile(tempfile_p);
		errno = save_errno;
		return -1;
	}

	deactivate_tempfile(tempfile);
	*tempfile_p = NULL;
	return 0;
}

void delete_tempfile(struct tempfile **tempfile_p)
{
	struct tempfile *tempfile = *tempfile_p;

	if (!is_tempfile_active(tempfile))
		return;

	close_tempfile_gently(tempfile);
	unlink_or_warn(tempfile->filename.buf);
	deactivate_tempfile(tempfile);
	*tempfile_p = NULL;
}
