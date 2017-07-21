#ifndef TEMPFILE_H
#define TEMPFILE_H

/*
 * Handle temporary files.
 *
 * The tempfile API allows temporary files to be created, deleted, and
 * atomically renamed. Temporary files that are still active when the
 * program ends are cleaned up automatically. Lockfiles (see
 * "lockfile.h") are built on top of this API.
 *
 *
 * Calling sequence
 * ----------------
 *
 * The caller:
 *
 * * Allocates a `struct tempfile` either as a static variable or on
 *   the heap, initialized to zeros. Once you use the structure to
 *   call `create_tempfile()`, it belongs to the tempfile subsystem
 *   and its storage must remain valid throughout the life of the
 *   program (i.e. you cannot use an on-stack variable to hold this
 *   structure).
 *
 * * Attempts to create a temporary file by calling
 *   `create_tempfile()`.
 *
 * * Writes new content to the file by either:
 *
 *   * writing to the file descriptor returned by `create_tempfile()`
 *     (also available via `tempfile->fd`).
 *
 *   * calling `fdopen_tempfile()` to get a `FILE` pointer for the
 *     open file and writing to the file using stdio.
 *
 *   Note that the file descriptor returned by create_tempfile()
 *   is marked O_CLOEXEC, so the new contents must be written by
 *   the current process, not any spawned one.
 *
 * When finished writing, the caller can:
 *
 * * Close the file descriptor and remove the temporary file by
 *   calling `delete_tempfile()`.
 *
 * * Close the temporary file and rename it atomically to a specified
 *   filename by calling `rename_tempfile()`. This relinquishes
 *   control of the file.
 *
 * * Close the file descriptor without removing or renaming the
 *   temporary file by calling `close_tempfile()`, and later call
 *   `delete_tempfile()` or `rename_tempfile()`.
 *
 * Even after the temporary file is renamed or deleted, the `tempfile`
 * object must not be freed or altered by the caller. However, it may
 * be reused; just pass it to another call of `create_tempfile()`.
 *
 * If the program exits before `rename_tempfile()` or
 * `delete_tempfile()` is called, an `atexit(3)` handler will close
 * and remove the temporary file.
 *
 * If you need to close the file descriptor yourself, do so by calling
 * `close_tempfile()`. You should never call `close(2)` or `fclose(3)`
 * yourself, otherwise the `struct tempfile` structure would still
 * think that the file descriptor needs to be closed, and a later
 * cleanup would result in duplicate calls to `close(2)`. Worse yet,
 * if you close and then later open another file descriptor for a
 * completely different purpose, then the unrelated file descriptor
 * might get closed.
 *
 *
 * Error handling
 * --------------
 *
 * `create_tempfile()` returns a file descriptor on success or -1 on
 * failure. On errors, `errno` describes the reason for failure.
 *
 * `delete_tempfile()`, `rename_tempfile()`, and `close_tempfile()`
 * return 0 on success. On failure they set `errno` appropriately, do
 * their best to delete the temporary file, and return -1.
 */

struct tempfile {
	struct tempfile *volatile next;
	volatile sig_atomic_t active;
	volatile int fd;
	FILE *volatile fp;
	volatile pid_t owner;
	char on_list;
	struct strbuf filename;
};

/*
 * Attempt to create a temporary file at the specified `path`. Return
 * a file descriptor for writing to it, or -1 on error. It is an error
 * if a file already exists at that path.
 */
extern int create_tempfile(struct tempfile *tempfile, const char *path);

/*
 * Register an existing file as a tempfile, meaning that it will be
 * deleted when the program exits. The tempfile is considered closed,
 * but it can be worked with like any other closed tempfile (for
 * example, it can be opened using reopen_tempfile()).
 */
extern void register_tempfile(struct tempfile *tempfile, const char *path);


/*
 * mks_tempfile functions
 *
 * The following functions attempt to create and open temporary files
 * with names derived automatically from a template, in the manner of
 * mkstemps(), and arrange for them to be deleted if the program ends
 * before they are deleted explicitly. There is a whole family of such
 * functions, named according to the following pattern:
 *
 *     x?mks_tempfile_t?s?m?()
 *
 * The optional letters have the following meanings:
 *
 *   x - die if the temporary file cannot be created.
 *
 *   t - create the temporary file under $TMPDIR (as opposed to
 *       relative to the current directory). When these variants are
 *       used, template should be the pattern for the filename alone,
 *       without a path.
 *
 *   s - template includes a suffix that is suffixlen characters long.
 *
 *   m - the temporary file should be created with the specified mode
 *       (otherwise, the mode is set to 0600).
 *
 * None of these functions modify template. If the caller wants to
 * know the (absolute) path of the file that was created, it can be
 * read from tempfile->filename.
 *
 * On success, the functions return a file descriptor that is open for
 * writing the temporary file. On errors, they return -1 and set errno
 * appropriately (except for the "x" variants, which die() on errors).
 */

/* See "mks_tempfile functions" above. */
extern int mks_tempfile_sm(struct tempfile *tempfile,
			   const char *template, int suffixlen, int mode);

/* See "mks_tempfile functions" above. */
static inline int mks_tempfile_s(struct tempfile *tempfile,
				 const char *template, int suffixlen)
{
	return mks_tempfile_sm(tempfile, template, suffixlen, 0600);
}

/* See "mks_tempfile functions" above. */
static inline int mks_tempfile_m(struct tempfile *tempfile,
				 const char *template, int mode)
{
	return mks_tempfile_sm(tempfile, template, 0, mode);
}

/* See "mks_tempfile functions" above. */
static inline int mks_tempfile(struct tempfile *tempfile,
			       const char *template)
{
	return mks_tempfile_sm(tempfile, template, 0, 0600);
}

/* See "mks_tempfile functions" above. */
extern int mks_tempfile_tsm(struct tempfile *tempfile,
			    const char *template, int suffixlen, int mode);

/* See "mks_tempfile functions" above. */
static inline int mks_tempfile_ts(struct tempfile *tempfile,
				  const char *template, int suffixlen)
{
	return mks_tempfile_tsm(tempfile, template, suffixlen, 0600);
}

/* See "mks_tempfile functions" above. */
static inline int mks_tempfile_tm(struct tempfile *tempfile,
				  const char *template, int mode)
{
	return mks_tempfile_tsm(tempfile, template, 0, mode);
}

/* See "mks_tempfile functions" above. */
static inline int mks_tempfile_t(struct tempfile *tempfile,
				 const char *template)
{
	return mks_tempfile_tsm(tempfile, template, 0, 0600);
}

/* See "mks_tempfile functions" above. */
extern int xmks_tempfile_m(struct tempfile *tempfile,
			   const char *template, int mode);

/* See "mks_tempfile functions" above. */
static inline int xmks_tempfile(struct tempfile *tempfile,
				const char *template)
{
	return xmks_tempfile_m(tempfile, template, 0600);
}

/*
 * Associate a stdio stream with the temporary file (which must still
 * be open). Return `NULL` (*without* deleting the file) on error. The
 * stream is closed automatically when `close_tempfile()` is called or
 * when the file is deleted or renamed.
 */
extern FILE *fdopen_tempfile(struct tempfile *tempfile, const char *mode);

static inline int is_tempfile_active(struct tempfile *tempfile)
{
	return tempfile->active;
}

/*
 * Return the path of the lockfile. The return value is a pointer to a
 * field within the lock_file object and should not be freed.
 */
extern const char *get_tempfile_path(struct tempfile *tempfile);

extern int get_tempfile_fd(struct tempfile *tempfile);
extern FILE *get_tempfile_fp(struct tempfile *tempfile);

/*
 * If the temporary file is still open, close it (and the file pointer
 * too, if it has been opened using `fdopen_tempfile()`) without
 * deleting the file. Return 0 upon success. On failure to `close(2)`,
 * return a negative value and delete the file. Usually
 * `delete_tempfile()` or `rename_tempfile()` should eventually be
 * called if `close_tempfile()` succeeds.
 */
extern int close_tempfile(struct tempfile *tempfile);

/*
 * Re-open a temporary file that has been closed using
 * `close_tempfile()` but not yet deleted or renamed. This can be used
 * to implement a sequence of operations like the following:
 *
 * * Create temporary file.
 *
 * * Write new contents to file, then `close_tempfile()` to cause the
 *   contents to be written to disk.
 *
 * * Pass the name of the temporary file to another program to allow
 *   it (and nobody else) to inspect or even modify the file's
 *   contents.
 *
 * * `reopen_tempfile()` to reopen the temporary file. Make further
 *   updates to the contents.
 *
 * * `rename_tempfile()` to move the file to its permanent location.
 */
extern int reopen_tempfile(struct tempfile *tempfile);

/*
 * Close the file descriptor and/or file pointer and remove the
 * temporary file associated with `tempfile`. It is a NOOP to call
 * `delete_tempfile()` for a `tempfile` object that has already been
 * deleted or renamed.
 */
extern void delete_tempfile(struct tempfile *tempfile);

/*
 * Close the file descriptor and/or file pointer if they are still
 * open, and atomically rename the temporary file to `path`. `path`
 * must be on the same filesystem as the lock file. Return 0 on
 * success. On failure, delete the temporary file and return -1, with
 * `errno` set to the value from the failing call to `close(2)` or
 * `rename(2)`. It is a bug to call `rename_tempfile()` for a
 * `tempfile` object that is not currently active.
 */
extern int rename_tempfile(struct tempfile *tempfile, const char *path);

#endif /* TEMPFILE_H */
