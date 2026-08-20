#include "git-compat-util.h"
#include "object-file.h"
#include "odb/source-files.h"
#include "odb/source.h"
#include "packfile.h"

static const char * const odb_source_names_by_type[] = {
	[ODB_SOURCE_UNKNOWN] = "unknown",
	[ODB_SOURCE_FILES] = "files",
	[ODB_SOURCE_LOOSE] = "loose",
	[ODB_SOURCE_PACKED] = "packed",
	[ODB_SOURCE_INMEMORY] = "in-memory",
};

const char *odb_source_type_to_name(enum odb_source_type type)
{
	const char *name;
	if (type < 0 || type >= ARRAY_SIZE(odb_source_names_by_type))
		type = ODB_SOURCE_UNKNOWN;
	name = odb_source_names_by_type[type];
	if (!name)
		BUG("name missing in `odb_source_names_by_type` for '%d'", type);
	return name;
}

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
