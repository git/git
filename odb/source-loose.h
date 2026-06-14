#ifndef ODB_SOURCE_LOOSE_H
#define ODB_SOURCE_LOOSE_H

#include "odb/source.h"

struct odb_source_files;
struct object_database;
struct oidtree;

/*
 * An object database source that stores its objects in loose format, one
 * file per object.
 */
struct odb_source_loose {
	struct odb_source base;

	/*
	 * Used to store the results of readdir(3) calls when we are OK
	 * sacrificing accuracy due to races for speed. That includes
	 * object existence with OBJECT_INFO_QUICK, as well as
	 * our search for unique abbreviated hashes. Don't use it for tasks
	 * requiring greater accuracy!
	 *
	 * Be sure to call odb_load_loose_cache() before using.
	 */
	uint32_t subdir_seen[8]; /* 256 bits */
	struct oidtree *cache;

	/* Map between object IDs for loose objects. */
	struct loose_object_map *map;
};

struct odb_source_loose *odb_source_loose_new(struct object_database *odb,
					      const char *path,
					      bool local);

/*
 * Cast the given object database source to the loose backend. This will cause
 * a BUG in case the source doesn't use this backend.
 */
static inline struct odb_source_loose *odb_source_loose_downcast(struct odb_source *source)
{
	if (source->type != ODB_SOURCE_LOOSE)
		BUG("trying to downcast source of type '%d' to loose", source->type);
	return container_of(source, struct odb_source_loose, base);
}

#endif
