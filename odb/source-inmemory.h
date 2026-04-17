#ifndef ODB_SOURCE_INMEMORY_H
#define ODB_SOURCE_INMEMORY_H

#include "odb/source.h"

struct oidtree;

/*
 * An in-memory source that you can write objects to that shall be made
 * available for reading, but that shouldn't ever be persisted to disk. Note
 * that any objects written to this source will be stored in memory, so the
 * number of objects you can store is limited by available system memory.
 */
struct odb_source_inmemory {
	struct odb_source base;
	struct oidtree *objects;
};

/* Create a new in-memory object database source. */
struct odb_source_inmemory *odb_source_inmemory_new(struct object_database *odb);

/*
 * Cast the given object database source to the in-memory backend. This will
 * cause a BUG in case the source doesn't use this backend.
 */
static inline struct odb_source_inmemory *odb_source_inmemory_downcast(struct odb_source *source)
{
	if (source->type != ODB_SOURCE_INMEMORY)
		BUG("trying to downcast source of type '%d' to in-memory", source->type);
	return container_of(source, struct odb_source_inmemory, base);
}

#endif
