#include "cache.h"
#include "run-command.h"
#include "xdiff-interface.h"
#include "ll-merge.h"
#include "blob.h"
#include "merge-blobs.h"

static int fill_mmfile_blob(mmfile_t *f, struct blob *obj)
{
	void *buf;
	unsigned long size;
	enum object_type type;

	buf = read_sha1_file(obj->object.oid.hash, &type, &size);
	if (!buf)
		return -1;
	if (type != OBJ_BLOB) {
		free(buf);
		return -1;
	}
	f->ptr = buf;
	f->size = size;
	return 0;
}

static void free_mmfile(mmfile_t *f)
{
	free(f->ptr);
}

static void *three_way_filemerge(const char *path, mmfile_t *base, mmfile_t *our, mmfile_t *their, unsigned long *size)
{
	int merge_status;
	mmbuffer_t res;

	/*
	 * This function is only used by cmd_merge_tree, which
	 * does not respect the merge.conflictstyle option.
	 * There is no need to worry about a label for the
	 * common ancestor.
	 */
	merge_status = ll_merge(&res, path, base, NULL,
				our, ".our", their, ".their", NULL);
	if (merge_status < 0)
		return NULL;

	*size = res.size;
	return res.ptr;
}

void *merge_blobs(const char *path, struct blob *base, struct blob *our, struct blob *their, unsigned long *size)
{
	void *res = NULL;
	mmfile_t f1, f2, common;

	/*
	 * Removed in either branch?
	 *
	 * NOTE! This depends on the caller having done the
	 * proper warning about removing a file that got
	 * modified in the other branch!
	 */
	if (!our || !their) {
		enum object_type type;
		if (base)
			return NULL;
		if (!our)
			our = their;
		return read_sha1_file(our->object.oid.hash, &type, size);
	}

	if (fill_mmfile_blob(&f1, our) < 0)
		goto out_no_mmfile;
	if (fill_mmfile_blob(&f2, their) < 0)
		goto out_free_f1;

	if (base) {
		if (fill_mmfile_blob(&common, base) < 0)
			goto out_free_f2_f1;
	} else {
		common.ptr = xstrdup("");
		common.size = 0;
	}
	res = three_way_filemerge(path, &common, &f1, &f2, size);
	free_mmfile(&common);
out_free_f2_f1:
	free_mmfile(&f2);
out_free_f1:
	free_mmfile(&f1);
out_no_mmfile:
	return res;
}
