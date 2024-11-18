#include "system.h"
#include "basics.h"
#include "reftable-error.h"
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
