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
	source->loose = odb_source_loose_new(source);
	source->packfiles = packfile_store_new(source);

	return source;
}

void odb_source_free(struct odb_source *source)
{
	free(source->path);
	odb_source_loose_free(source->loose);
	packfile_store_free(source->packfiles);
	free(source);
}
