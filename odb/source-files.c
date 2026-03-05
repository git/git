#include "git-compat-util.h"
#include "abspath.h"
#include "chdir-notify.h"
#include "object-file.h"
#include "odb.h"
#include "odb/source.h"
#include "odb/source-files.h"
#include "packfile.h"
#include "strbuf.h"

static void odb_source_files_reparent(const char *name UNUSED,
				      const char *old_cwd,
				      const char *new_cwd,
				      void *cb_data)
{
	struct odb_source_files *files = cb_data;
	char *path = reparent_relative_path(old_cwd, new_cwd,
					    files->base.path);
	free(files->base.path);
	files->base.path = path;
}

static void odb_source_files_free(struct odb_source *source)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	chdir_notify_unregister(NULL, odb_source_files_reparent, files);
	odb_source_loose_free(files->loose);
	packfile_store_free(files->packed);
	odb_source_release(&files->base);
	free(files);
}

static void odb_source_files_close(struct odb_source *source)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	packfile_store_close(files->packed);
}

static void odb_source_files_reprepare(struct odb_source *source)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	odb_source_loose_reprepare(&files->base);
	packfile_store_reprepare(files->packed);
}

static int odb_source_files_read_object_info(struct odb_source *source,
					     const struct object_id *oid,
					     struct object_info *oi,
					     enum object_info_flags flags)
{
	struct odb_source_files *files = odb_source_files_downcast(source);

	if (!packfile_store_read_object_info(files->packed, oid, oi, flags) ||
	    !odb_source_loose_read_object_info(source, oid, oi, flags))
		return 0;

	return -1;
}

static int odb_source_files_read_object_stream(struct odb_read_stream **out,
					       struct odb_source *source,
					       const struct object_id *oid)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	if (!packfile_store_read_object_stream(out, files->packed, oid) ||
	    !odb_source_loose_read_object_stream(out, source, oid))
		return 0;
	return -1;
}

static int odb_source_files_for_each_object(struct odb_source *source,
					    const struct object_info *request,
					    odb_for_each_object_cb cb,
					    void *cb_data,
					    unsigned flags)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	int ret;

	if (!(flags & ODB_FOR_EACH_OBJECT_PROMISOR_ONLY)) {
		ret = odb_source_loose_for_each_object(source, request, cb, cb_data, flags);
		if (ret)
			return ret;
	}

	ret = packfile_store_for_each_object(files->packed, request, cb, cb_data, flags);
	if (ret)
		return ret;

	return 0;
}

static int odb_source_files_freshen_object(struct odb_source *source,
					   const struct object_id *oid)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	if (packfile_store_freshen_object(files->packed, oid) ||
	    odb_source_loose_freshen_object(source, oid))
		return 1;
	return 0;
}

static int odb_source_files_write_object(struct odb_source *source,
					 const void *buf, unsigned long len,
					 enum object_type type,
					 struct object_id *oid,
					 struct object_id *compat_oid,
					 unsigned flags)
{
	return odb_source_loose_write_object(source, buf, len, type,
					     oid, compat_oid, flags);
}

static int odb_source_files_write_object_stream(struct odb_source *source,
						struct odb_write_stream *stream,
						size_t len,
						struct object_id *oid)
{
	return odb_source_loose_write_stream(source, stream, len, oid);
}

static int odb_source_files_read_alternates(struct odb_source *source,
					    struct strvec *out)
{
	struct strbuf buf = STRBUF_INIT;
	char *path;

	path = xstrfmt("%s/info/alternates", source->path);
	if (strbuf_read_file(&buf, path, 1024) < 0) {
		warn_on_fopen_errors(path);
		free(path);
		return 0;
	}
	parse_alternates(buf.buf, '\n', source->path, out);

	strbuf_release(&buf);
	free(path);
	return 0;
}

struct odb_source_files *odb_source_files_new(struct object_database *odb,
					      const char *path,
					      bool local)
{
	struct odb_source_files *files;

	CALLOC_ARRAY(files, 1);
	odb_source_init(&files->base, odb, ODB_SOURCE_FILES, path, local);
	files->loose = odb_source_loose_new(&files->base);
	files->packed = packfile_store_new(&files->base);

	files->base.free = odb_source_files_free;
	files->base.close = odb_source_files_close;
	files->base.reprepare = odb_source_files_reprepare;
	files->base.read_object_info = odb_source_files_read_object_info;
	files->base.read_object_stream = odb_source_files_read_object_stream;
	files->base.for_each_object = odb_source_files_for_each_object;
	files->base.freshen_object = odb_source_files_freshen_object;
	files->base.write_object = odb_source_files_write_object;
	files->base.write_object_stream = odb_source_files_write_object_stream;
	files->base.read_alternates = odb_source_files_read_alternates;

	/*
	 * Ideally, we would only ever store absolute paths in the source. This
	 * is not (yet) possible though because we access and assume relative
	 * paths in the primary ODB source in some user-facing functionality.
	 */
	if (!is_absolute_path(path))
		chdir_notify_register(NULL, odb_source_files_reparent, files);

	return files;
}
