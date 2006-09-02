#include "cache.h"
#include "run-command.h"
#include "xdiff-interface.h"
#include "blob.h"

static void rm_temp_file(const char *filename)
{
	unlink(filename);
	free((void *)filename);
}

static const char *write_temp_file(mmfile_t *f)
{
	int fd;
	const char *tmp = getenv("TMPDIR");
	char *filename;

	if (!tmp)
		tmp = "/tmp";
	filename = mkpath("%s/%s", tmp, "git-tmp-XXXXXX");
	fd = mkstemp(filename);
	if (fd < 0)
		return NULL;
	filename = xstrdup(filename);
	if (f->size != xwrite(fd, f->ptr, f->size)) {
		rm_temp_file(filename);
		return NULL;
	}
	close(fd);
	return filename;
}

static void *read_temp_file(const char *filename, unsigned long *size)
{
	struct stat st;
	char *buf = NULL;
	int fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;
	if (!fstat(fd, &st)) {
		*size = st.st_size;
		buf = xmalloc(st.st_size);
		if (st.st_size != xread(fd, buf, st.st_size)) {
			free(buf);
			buf = NULL;
		}
	}
	close(fd);
	return buf;
}

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
	void *res;
	const char *t1, *t2, *t3;

	t1 = write_temp_file(base);
	t2 = write_temp_file(our);
	t3 = write_temp_file(their);
	res = NULL;
	if (t1 && t2 && t3) {
		int code = run_command("merge", t2, t1, t3, NULL);
		if (!code || code == -1)
			res = read_temp_file(t2, size);
	}
	rm_temp_file(t1);
	rm_temp_file(t2);
	rm_temp_file(t3);
	return res;
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
