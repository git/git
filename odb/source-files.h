#ifndef ODB_SOURCE_FILES_H
#define ODB_SOURCE_FILES_H

#include "odb/source.h"

struct odb_source_loose;
struct packfile_store;

/*
 * The files object database source uses a combination of loose objects and
 * packfiles. It is the default backend used by Git to store objects.
 */
struct odb_source_files {
	struct odb_source base;
	struct odb_source_loose *loose;
	struct packfile_store *packed;
};

/* Allocate and initialize a new object source. */
struct odb_source_files *odb_source_files_new(struct object_database *odb,
					      const char *path,
					      bool local);

/*
 * Cast the given object database source to the files backend. This will cause
 * a BUG in case the source doesn't use this backend.
 */
static inline struct odb_source_files *odb_source_files_downcast(struct odb_source *source)
{
	if (source->type != ODB_SOURCE_FILES)
		BUG("trying to downcast source of type '%d' to files", source->type);
	return container_of(source, struct odb_source_files, base);
}

#endif
