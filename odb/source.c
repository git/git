#include "git-compat-util.h"
#include "object-file.h"
#include "odb/source.h"
#include "packfile.h"

struct odb_source *odb_source_new(struct object_database *odb,
				  const char *path,
				  bool local)
{
	struct odb_source *source;

	CALLOC_ARRAY(source, 1);
	source->odb = odb;
	source->local = local;
	source->path = xstrdup(path);
	source->files = odb_source_files_new(source);

	return source;
}

void odb_source_free(struct odb_source *source)
{
	free(source->path);
	odb_source_files_free(source->files);
	free(source);
}
