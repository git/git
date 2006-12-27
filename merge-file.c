#include "cache.h"
#include "run-command.h"
#include "xdiff-interface.h"
#include "blob.h"

static int fill_mmfile_blob(mmfile_t *f, struct blob *obj)
{
	void *buf;
	unsigned long size;
	char type[20];

	buf = read_sha1_file(obj->object.sha1, type, &size);
	if (!buf)
		return -1;
	if (strcmp(type, blob_type))
		return -1;
	f->ptr = buf;
	f->size = size;
	return 0;
}

static void free_mmfile(mmfile_t *f)
{
	free(f->ptr);
}

static void *three_way_filemerge(mmfile_t *base, mmfile_t *our, mmfile_t *their, unsigned long *size)
{
	mmbuffer_t res;
	xpparam_t xpp;
	int merge_status;

	memset(&xpp, 0, sizeof(xpp));
	merge_status = xdl_merge(base, our, ".our", their, ".their",
		&xpp, XDL_MERGE_ZEALOUS, &res);

	if (merge_status < 0)
		return NULL;

	*size = res.size;
	return res.ptr;
}

static int common_outf(void *priv_, mmbuffer_t *mb, int nbuf)
{
	int i;
	mmfile_t *dst = priv_;

	for (i = 0; i < nbuf; i++) {
		memcpy(dst->ptr + dst->size, mb[i].ptr, mb[i].size);
		dst->size += mb[i].size;
	}
	return 0;
}

static int generate_common_file(mmfile_t *res, mmfile_t *f1, mmfile_t *f2)
{
	unsigned long size = f1->size < f2->size ? f1->size : f2->size;
	void *ptr = xmalloc(size);
	xpparam_t xpp;
	xdemitconf_t xecfg;
	xdemitcb_t ecb;

	xpp.flags = XDF_NEED_MINIMAL;
	xecfg.ctxlen = 3;
	xecfg.flags = XDL_EMIT_COMMON;
	ecb.outf = common_outf;

	res->ptr = ptr;
	res->size = 0;

	ecb.priv = res;
	return xdl_diff(f1, f2, &xpp, &xecfg, &ecb);
}

void *merge_file(struct blob *base, struct blob *our, struct blob *their, unsigned long *size)
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
		char type[20];
		if (base)
			return NULL;
		if (!our)
			our = their;
		return read_sha1_file(our->object.sha1, type, size);
	}

	if (fill_mmfile_blob(&f1, our) < 0)
		goto out_no_mmfile;
	if (fill_mmfile_blob(&f2, their) < 0)
		goto out_free_f1;

	if (base) {
		if (fill_mmfile_blob(&common, base) < 0)
			goto out_free_f2_f1;
	} else {
		if (generate_common_file(&common, &f1, &f2) < 0)
			goto out_free_f2_f1;
	}
	res = three_way_filemerge(&common, &f1, &f2, size);
	free_mmfile(&common);
out_free_f2_f1:
	free_mmfile(&f2);
out_free_f1:
	free_mmfile(&f1);
out_no_mmfile:
	return res;
}
