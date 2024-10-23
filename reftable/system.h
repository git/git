/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef SYSTEM_H
#define SYSTEM_H

/* This header glues the reftable library to the rest of Git */

#include "git-compat-util.h"
#include "lockfile.h"

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
 * active. Return 0 on success, a reftable error code on error.
 */
int tmpfile_rename(struct reftable_tmpfile *t, const char *path);

#endif
