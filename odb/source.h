#ifndef ODB_SOURCE_H
#define ODB_SOURCE_H

#include "odb/source-files.h"

/*
 * The source is the part of the object database that stores the actual
 * objects. It thus encapsulates the logic to read and write the specific
 * on-disk format. An object database can have multiple sources:
 *
 *   - The primary source, which is typically located in "$GIT_DIR/objects".
 *     This is where new objects are usually written to.
 *
 *   - Alternate sources, which are configured via "objects/info/alternates" or
 *     via the GIT_ALTERNATE_OBJECT_DIRECTORIES environment variable. These
 *     alternate sources are only used to read objects.
 */
struct odb_source {
	struct odb_source *next;

	/* Object database that owns this object source. */
	struct object_database *odb;

	/* The backend used to store objects. */
	struct odb_source_files *files;

	/*
	 * Figure out whether this is the local source of the owning
	 * repository, which would typically be its ".git/objects" directory.
	 * This local object directory is usually where objects would be
	 * written to.
	 */
	bool local;

	/*
	 * This object store is ephemeral, so there is no need to fsync.
	 */
	int will_destroy;

	/*
	 * Path to the source. If this is a relative path, it is relative to
	 * the current working directory.
	 */
	char *path;
};

/*
 * Allocate and initialize a new source for the given object database located
 * at `path`. `local` indicates whether or not the source is the local and thus
 * primary object source of the object database.
 */
struct odb_source *odb_source_new(struct object_database *odb,
				  const char *path,
				  bool local);

/* Free the object database source, releasing all associated resources. */
void odb_source_free(struct odb_source *source);

#endif
