#include "git-compat-util.h"
#include "object-file.h"
#include "odb/source.h"
#include "odb/source-files.h"
#include "packfile.h"

void odb_source_files_free(struct odb_source_files *files)
{
	if (!files)
		return;
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
	odb_source_init(&files->base, odb, path, local);
	files->loose = odb_source_loose_new(&files->base);
	files->packed = packfile_store_new(&files->base);

	return files;
}
