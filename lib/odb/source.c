#include "git-compat-util.h"
#include "object-file.h"
#include "odb/source-files.h"
#include "odb/source.h"
#include "packfile.h"

struct odb_source *odb_source_new(struct object_database *odb,
				  const char *path,
				  bool local)
{
	return &odb_source_files_new(odb, path, local)->base;
}

void odb_source_init(struct odb_source *source,
		     struct object_database *odb,
		     enum odb_source_type type,
		     const char *path,
		     bool local)
{
	source->odb = odb;
	source->type = type;
	source->local = local;
	source->path = xstrdup(path);
}

void odb_source_free(struct odb_source *source)
{
	if (!source)
		return;
	source->free(source);
}

void odb_source_release(struct odb_source *source)
{
	if (!source)
		return;
	free(source->path);
}
