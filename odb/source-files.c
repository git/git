#include "git-compat-util.h"
#include "object-file.h"
#include "odb/source-files.h"
#include "packfile.h"

void odb_source_files_free(struct odb_source_files *files)
{
	if (!files)
		return;
	odb_source_loose_free(files->loose);
	packfile_store_free(files->packed);
	free(files);
}

struct odb_source_files *odb_source_files_new(struct odb_source *source)
{
	struct odb_source_files *files;
	CALLOC_ARRAY(files, 1);
	files->source = source;
	files->loose = odb_source_loose_new(source);
	files->packed = packfile_store_new(source);
	return files;
}
