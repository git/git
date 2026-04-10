#include "git-compat-util.h"
#include "odb/source-inmemory.h"

static void odb_source_inmemory_free(struct odb_source *source)
{
	struct odb_source_inmemory *inmemory = odb_source_inmemory_downcast(source);
	for (size_t i = 0; i < inmemory->objects_nr; i++)
		free((char *) inmemory->objects[i].value.buf);
	free(inmemory->objects);
	free(inmemory->base.path);
	free(inmemory);
}

struct odb_source_inmemory *odb_source_inmemory_new(struct object_database *odb)
{
	struct odb_source_inmemory *source;

	CALLOC_ARRAY(source, 1);
	odb_source_init(&source->base, odb, ODB_SOURCE_INMEMORY, "source", false);

	source->base.free = odb_source_inmemory_free;

	return source;
}
