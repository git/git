#include "system.h"
#include "basics.h"
#include "reftable-error.h"
#include "../lockfile.h"
#include "../tempfile.h"

int tmpfile_from_pattern(struct reftable_tmpfile *out, const char *pattern)
{
	struct tempfile *tempfile;

	tempfile = mks_tempfile(pattern);
	if (!tempfile)
		return REFTABLE_IO_ERROR;

	out->path = tempfile->filename.buf;
	out->fd = tempfile->fd;
	out->priv = tempfile;

	return 0;
}

int tmpfile_close(struct reftable_tmpfile *t)
{
	struct tempfile *tempfile = t->priv;
	int ret = close_tempfile_gently(tempfile);
	t->fd = -1;
	if (ret < 0)
		return REFTABLE_IO_ERROR;
	return 0;
}

int tmpfile_delete(struct reftable_tmpfile *t)
{
	struct tempfile *tempfile = t->priv;
	int ret = delete_tempfile(&tempfile);
	*t = REFTABLE_TMPFILE_INIT;
	if (ret < 0)
		return REFTABLE_IO_ERROR;
	return 0;
}

int tmpfile_rename(struct reftable_tmpfile *t, const char *path)
{
	struct tempfile *tempfile = t->priv;
	int ret = rename_tempfile(&tempfile, path);
	*t = REFTABLE_TMPFILE_INIT;
	if (ret < 0)
		return REFTABLE_IO_ERROR;
	return 0;
}

int flock_acquire(struct reftable_flock *l, const char *target_path,
		  long timeout_ms)
{
	struct lock_file *lockfile;
	int err;

	lockfile = reftable_malloc(sizeof(*lockfile));
	if (!lockfile)
		return REFTABLE_OUT_OF_MEMORY_ERROR;

	err = hold_lock_file_for_update_timeout(lockfile, target_path, LOCK_NO_DEREF,
						timeout_ms);
	if (err < 0) {
		reftable_free(lockfile);
		if (errno == EEXIST)
			return REFTABLE_LOCK_ERROR;
		return -1;
	}

	l->fd = get_lock_file_fd(lockfile);
	l->path = get_lock_file_path(lockfile);
	l->priv = lockfile;

	return 0;
}

int flock_close(struct reftable_flock *l)
{
	struct lock_file *lockfile = l->priv;
	int ret;

	if (!lockfile)
		return REFTABLE_API_ERROR;

	ret = close_lock_file_gently(lockfile);
	l->fd = -1;
	if (ret < 0)
		return REFTABLE_IO_ERROR;

	return 0;
}

int flock_release(struct reftable_flock *l)
{
	struct lock_file *lockfile = l->priv;
	int ret;

	if (!lockfile)
		return 0;

	ret = rollback_lock_file(lockfile);
	reftable_free(lockfile);
	*l = REFTABLE_FLOCK_INIT;
	if (ret < 0)
		return REFTABLE_IO_ERROR;

	return 0;
}

int flock_commit(struct reftable_flock *l)
{
	struct lock_file *lockfile = l->priv;
	int ret;

	if (!lockfile)
		return REFTABLE_API_ERROR;

	ret = commit_lock_file(lockfile);
	reftable_free(lockfile);
	*l = REFTABLE_FLOCK_INIT;
	if (ret < 0)
		return REFTABLE_IO_ERROR;

	return 0;
}
