#ifndef ODB_SOURCE_FILES_H
#define ODB_SOURCE_FILES_H

#include "odb/source.h"

struct odb_source_loose;
struct odb_source_packed;

/*
 * The files object database source uses a combination of loose objects and
 * packfiles. It is the default backend used by Git to store objects.
 */
struct odb_source_files {
	struct odb_source base;
	struct odb_source_loose *loose;
	struct odb_source_packed *packed;
};

/* Allocate and initialize a new object source. */
struct odb_source_files *odb_source_files_new(struct object_database *odb,
					      const char *path,
					      bool local);

/*
 * Optimize the files object database source by repacking loose objects and
 * packfiles as needed. Returns 0 on success, a negative error code otherwise.
 */
int odb_source_files_optimize(struct odb_source *source,
			      const struct odb_optimize_options *opts);

/*
 * Check whether optimization of the files object database source is required
 * given the provided options. Returns true if optimization should be
 * performed, false otherwise.
 */
bool odb_source_files_optimize_required(struct odb_source *source,
					const struct odb_optimize_options *opts);

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
