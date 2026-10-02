#ifndef ODB_SOURCE_FILES_H
#define ODB_SOURCE_FILES_H

#include "odb/source.h"

struct odb_source_loose;
struct odb_source_packed;

/*
 * A single object directory that encapsulates access to both the loose and
 * packed backend. This can either be the primary or an alternate object
 * directory.
 */
struct odb_files_dir {
	/* Absolute path to the object directory. */
	char *abspath;

	/* The two sources derived from this object directory. */
	struct odb_source_loose *loose;
	struct odb_source_packed *packed;

	/*
	 * Whether this is the local object directory of the owning
	 * repository. Directories added via alternates are not local.
	 */
	bool local;
};

struct odb_files_dir *odb_files_dir_new(struct object_database *odb,
					const char *path, bool local);
void odb_files_dir_free(struct odb_files_dir *dir);

/*
 * The files object database source uses a combination of loose objects and
 * packfiles. It is the default backend used by Git to store objects.
 */
struct odb_source_files {
	struct odb_source base;
	struct odb_files_dir *dirs;
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
		BUG("trying to downcast source of type '%s' to '%s'",
		    odb_source_type_to_name(source->type),
		    odb_source_type_to_name(ODB_SOURCE_FILES));
	return container_of(source, struct odb_source_files, base);
}

#endif
