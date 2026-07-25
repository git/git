#include "git-compat-util.h"
#include "abspath.h"
#include "chdir-notify.h"
#include "gettext.h"
#include "lockfile.h"
#include "object-file.h"
#include "odb.h"
#include "odb/source.h"
#include "odb/source-files.h"
#include "odb/source-loose.h"
#include "packfile.h"
#include "strbuf.h"
#include "write-or-die.h"

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
	odb_source_free(&files->loose->base);
	odb_source_free(&files->packed->base);
	odb_source_release(&files->base);
	free(files);
}

static void odb_source_files_close(struct odb_source *source)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	odb_source_close(&files->loose->base);
	odb_source_close(&files->packed->base);
}

static void odb_source_files_prepare(struct odb_source *source,
				     enum odb_prepare_flags flags)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	odb_source_prepare(&files->loose->base, flags);
	odb_source_prepare(&files->packed->base, flags);
}

static int odb_source_files_read_object_info(struct odb_source *source,
					     const struct object_id *oid,
					     struct object_info *oi,
					     enum object_info_flags flags)
{
	struct odb_source_files *files = odb_source_files_downcast(source);

	if (!odb_source_read_object_info(&files->packed->base, oid, oi, flags) ||
	    !odb_source_read_object_info(&files->loose->base, oid, oi, flags))
		return 0;

	return -1;
}

static int odb_source_files_read_object_stream(struct odb_read_stream **out,
					       struct odb_source *source,
					       const struct object_id *oid)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	if (!odb_source_read_object_stream(out, &files->packed->base, oid) ||
	    !odb_source_read_object_stream(out, &files->loose->base, oid))
		return 0;
	return -1;
}

static int odb_source_files_for_each_object(struct odb_source *source,
					    const struct object_info *request,
					    odb_for_each_object_cb cb,
					    void *cb_data,
					    const struct odb_for_each_object_options *opts)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	int ret;

	if (!(opts->flags & ODB_FOR_EACH_OBJECT_PROMISOR_ONLY)) {
		ret = odb_source_for_each_object(&files->loose->base, request, cb, cb_data, opts);
		if (ret)
			return ret;
	}

	ret = odb_source_for_each_object(&files->packed->base, request, cb, cb_data, opts);
	if (ret)
		return ret;

	return 0;
}

static int odb_source_files_count_objects(struct odb_source *source,
					  enum odb_count_objects_flags flags,
					  unsigned long *out)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	unsigned long count;
	int ret;

	ret = odb_source_count_objects(&files->packed->base, flags, &count);
	if (ret < 0)
		goto out;

	if (!(flags & ODB_COUNT_OBJECTS_APPROXIMATE)) {
		unsigned long loose_count;

		ret = odb_source_count_objects(&files->loose->base, flags, &loose_count);
		if (ret < 0)
			goto out;

		count += loose_count;
	}

	*out = count;
	ret = 0;

out:
	return ret;
}

static int odb_source_files_find_abbrev_len(struct odb_source *source,
					    const struct object_id *oid,
					    unsigned min_len,
					    unsigned *out)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	unsigned len = min_len;
	int ret;

	ret = odb_source_find_abbrev_len(&files->packed->base, oid, len, &len);
	if (ret < 0)
		goto out;

	ret = odb_source_find_abbrev_len(&files->loose->base, oid, len, &len);
	if (ret < 0)
		goto out;

	*out = len;
	ret = 0;

out:
	return ret;
}

static int odb_source_files_freshen_object(struct odb_source *source,
					   const struct object_id *oid)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	if (odb_source_freshen_object(&files->packed->base, oid) ||
	    odb_source_freshen_object(&files->loose->base, oid))
		return 1;
	return 0;
}

static int odb_source_files_write_object(struct odb_source *source,
					 const void *buf, size_t len,
					 enum object_type type,
					 struct object_id *oid,
					 struct object_id *compat_oid,
					 enum odb_write_object_flags flags)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	return odb_source_write_object(&files->loose->base, buf, len, type,
				       oid, compat_oid, flags);
}

static int odb_source_files_write_object_stream(struct odb_source *source,
						struct odb_write_stream *stream,
						size_t len,
						struct object_id *oid)
{
	struct odb_source_files *files = odb_source_files_downcast(source);
	return odb_source_write_object_stream(&files->loose->base, stream, len, oid);
}

static int odb_source_files_begin_transaction(struct odb_source *source,
					      struct odb_transaction **out,
					      enum odb_transaction_flags flags)
{
	return odb_transaction_files_begin(source, out, flags);
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

static int odb_source_files_write_alternate(struct odb_source *source,
					    const char *alternate)
{
	struct lock_file lock = LOCK_INIT;
	char *path = xstrfmt("%s/%s", source->path, "info/alternates");
	FILE *in, *out;
	int found = 0;
	int ret;

	hold_lock_file_for_update(&lock, path, LOCK_DIE_ON_ERROR);
	out = fdopen_lock_file(&lock, "w");
	if (!out) {
		ret = error_errno(_("unable to fdopen alternates lockfile"));
		goto out;
	}

	in = fopen(path, "r");
	if (in) {
		struct strbuf line = STRBUF_INIT;

		while (strbuf_getline(&line, in) != EOF) {
			if (!strcmp(alternate, line.buf)) {
				found = 1;
				break;
			}
			fprintf_or_die(out, "%s\n", line.buf);
		}

		strbuf_release(&line);
		fclose(in);
	} else if (errno != ENOENT) {
		ret = error_errno(_("unable to read alternates file"));
		goto out;
	}

	if (found) {
		rollback_lock_file(&lock);
	} else {
		fprintf_or_die(out, "%s\n", alternate);
		if (commit_lock_file(&lock)) {
			ret = error_errno(_("unable to move new alternates file into place"));
			goto out;
		}
	}

	ret = 0;

out:
	free(path);
	return ret;
}

struct odb_source_files *odb_source_files_new(struct object_database *odb,
					      const char *path,
					      bool local)
{
	struct odb_source_files *files;

	CALLOC_ARRAY(files, 1);
	odb_source_init(&files->base, odb, ODB_SOURCE_FILES, path, local);
	files->loose = odb_source_loose_new(odb, path, local);
	files->packed = odb_source_packed_new(odb, path, local);

	files->base.free = odb_source_files_free;
	files->base.close = odb_source_files_close;
	files->base.prepare = odb_source_files_prepare;
	files->base.read_object_info = odb_source_files_read_object_info;
	files->base.read_object_stream = odb_source_files_read_object_stream;
	files->base.for_each_object = odb_source_files_for_each_object;
	files->base.count_objects = odb_source_files_count_objects;
	files->base.find_abbrev_len = odb_source_files_find_abbrev_len;
	files->base.freshen_object = odb_source_files_freshen_object;
	files->base.write_object = odb_source_files_write_object;
	files->base.write_object_stream = odb_source_files_write_object_stream;
	files->base.begin_transaction = odb_source_files_begin_transaction;
	files->base.read_alternates = odb_source_files_read_alternates;
	files->base.write_alternate = odb_source_files_write_alternate;

	/*
	 * Ideally, we would only ever store absolute paths in the source. This
	 * is not (yet) possible though because we access and assume relative
	 * paths in the primary ODB source in some user-facing functionality.
	 */
	if (!is_absolute_path(path))
		chdir_notify_register(NULL, odb_source_files_reparent, files);

	return files;
}
