/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef SYSTEM_H
#define SYSTEM_H

/* This header glues the reftable library to the rest of Git */

#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"

/*
 * An implementation-specific temporary file. By making this specific to the
 * implementation it becomes possible to tie temporary files into any kind of
 * signal or atexit handlers for cleanup on abnormal situations.
 */
struct reftable_tmpfile {
	const char *path;
	int fd;
	void *priv;
};
#define REFTABLE_TMPFILE_INIT ((struct reftable_tmpfile) { .fd = -1, })

/*
 * Create a temporary file from a pattern similar to how mkstemp(3p) would.
 * The `pattern` shall not be modified. On success, the structure at `out` has
 * been initialized such that it is ready for use. Returns 0 on success, a
 * reftable error code on error.
 */
int tmpfile_from_pattern(struct reftable_tmpfile *out, const char *pattern);

/*
 * Close the temporary file's file descriptor without removing the file itself.
 * This is a no-op in case the file has already been closed beforehand. Returns
 * 0 on success, a reftable error code on error.
 */
int tmpfile_close(struct reftable_tmpfile *t);

/*
 * Close the temporary file and delete it. This is a no-op in case the file has
 * already been deleted or renamed beforehand. Returns 0 on success, a reftable
 * error code on error.
 */
int tmpfile_delete(struct reftable_tmpfile *t);

/*
 * Rename the temporary file to the provided path. The temporary file must be
 * active. Return 0 on success, a reftable error code on error. Deactivates the
 * temporary file.
 */
int tmpfile_rename(struct reftable_tmpfile *t, const char *path);

/*
 * An implementation-specific file lock. Same as with `reftable_tmpfile`,
 * making this specific to the implementation makes it possible to tie this
 * into signal or atexit handlers such that we know to clean up stale locks on
 * abnormal exits.
 */
struct reftable_flock {
	const char *path;
	int fd;
	void *priv;
};
#define REFTABLE_FLOCK_INIT ((struct reftable_flock){ .fd = -1, })

/*
 * Acquire the lock for the given target path by exclusively creating a file
 * with ".lock" appended to it. If that lock exists, we wait up to `timeout_ms`
 * to acquire the lock. If `timeout_ms` is 0 we don't wait, if it is negative
 * we block indefinitely.
 *
 * Retrun 0 on success, a reftable error code on error.
 */
int flock_acquire(struct reftable_flock *l, const char *target_path,
		  long timeout_ms);

/*
 * Close the lockfile's file descriptor without removing the lock itself. This
 * is a no-op in case the lockfile has already been closed beforehand. Returns
 * 0 on success, a reftable error code on error.
 */
int flock_close(struct reftable_flock *l);

/*
 * Release the lock by unlinking the lockfile. This is a no-op in case the
 * lockfile has already been released or committed beforehand. Returns 0 on
 * success, a reftable error code on error.
 */
int flock_release(struct reftable_flock *l);

/*
 * Commit the lock by renaming the lockfile into place. Returns 0 on success, a
 * reftable error code on error.
 */
int flock_commit(struct reftable_flock *l);

#endif
