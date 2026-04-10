#include "git-compat-util.h"
#include "odb/source-inmemory.h"

struct odb_source_inmemory *odb_source_inmemory_new(struct object_database *odb)
{
	struct odb_source_inmemory *source;

	CALLOC_ARRAY(source, 1);
	odb_source_init(&source->base, odb, ODB_SOURCE_INMEMORY, "source", false);

	return source;
}
