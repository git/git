#ifndef ODB_SOURCE_PACKED_H
#define ODB_SOURCE_PACKED_H

#include "odb/source.h"
#include "packfile-list.h"
#include "strmap.h"

/*
 * A store that manages packfiles for a given object database.
 */
struct odb_source_packed {
	struct odb_source base;

	/*
	 * The list of packfiles in the order in which they have been most
	 * recently used.
	 */
	struct packfile_list packs;

	/*
	 * Cache of packfiles which are marked as "kept", either because there
	 * is an on-disk ".keep" file or because they are marked as "kept" in
	 * memory.
	 *
	 * Should not be accessed directly, but via
	 * `packfile_store_get_kept_pack_cache()`. The list of packs gets
	 * invalidated when the stored flags and the flags passed to
	 * `packfile_store_get_kept_pack_cache()` mismatch.
	 */
	struct {
		struct packed_git **packs;
		unsigned flags;
	} kept_cache;

	/* The multi-pack index that belongs to this specific packfile store. */
	struct multi_pack_index *midx;

	/*
	 * A map of packfile names to packed_git structs for tracking which
	 * packs have been loaded already.
	 */
	struct strmap packs_by_path;

	/*
	 * Whether packfiles have already been populated with this store's
	 * packs.
	 */
	bool initialized;

	/*
	 * Usually, packfiles will be reordered to the front of the `packs`
	 * list whenever an object is looked up via them. This has the effect
	 * that packs that contain a lot of accessed objects will be located
	 * towards the front.
	 *
	 * This is usually desirable, but there are exceptions. One exception
	 * is when the looking up multiple objects in a loop for each packfile.
	 * In that case, we may easily end up with an infinite loop as the
	 * packfiles get reordered to the front repeatedly.
	 *
	 * Setting this field to `true` thus disables these reorderings.
	 */
	bool skip_mru_updates;
};

/*
 * Allocate and initialize a new empty packfile store for the given object
 * database.
 */
struct odb_source_packed *odb_source_packed_new(struct object_database *odb,
						const char *path,
						bool local);

/*
 * Cast the given object database source to the packed backend. This will cause
 * a BUG in case the source doesn't use this backend.
 */
static inline struct odb_source_packed *odb_source_packed_downcast(struct odb_source *source)
{
	if (source->type != ODB_SOURCE_PACKED)
		BUG("trying to downcast source of type '%d' to packed", source->type);
	return container_of(source, struct odb_source_packed, base);
}

#endif
