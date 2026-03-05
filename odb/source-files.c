#include "git-compat-util.h"
#include "abspath.h"
#include "chdir-notify.h"
#include "object-file.h"
#include "odb/source.h"
#include "odb/source-files.h"
#include "packfile.h"

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

void odb_source_files_free(struct odb_source_files *files)
{
	if (!files)
		return;
	chdir_notify_unregister(NULL, odb_source_files_reparent, files);
	odb_source_loose_free(files->loose);
	packfile_store_free(files->packed);
	odb_source_release(&files->base);
	free(files);
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

	/*
	 * Ideally, we would only ever store absolute paths in the source. This
	 * is not (yet) possible though because we access and assume relative
	 * paths in the primary ODB source in some user-facing functionality.
	 */
	if (!is_absolute_path(path))
		chdir_notify_register(NULL, odb_source_files_reparent, files);

	return files;
}
