#ifndef REPLACE_OBJECT_H
#define REPLACE_OBJECT_H

#include "oidmap.h"
#include "repository.h"
#include "object-store-ll.h"

struct replace_object {
	struct oidmap_entry original;
	struct object_id replacement;
};

void prepare_replace_object(struct repository *r);

/*
 * This internal function is only declared here for the benefit of
 * lookup_replace_object().  Please do not call it directly.
 */
const struct object_id *do_lookup_replace_object(struct repository *r,
						 const struct object_id *oid);

/*
 * Some commands disable replace-refs unconditionally, and otherwise each
 * repository could alter the core.useReplaceRefs config value.
 *
 * Return 1 if and only if all of the following are true:
 *
 *  a. disable_replace_refs() has not been called.
 *  b. GIT_NO_REPLACE_OBJECTS is unset or zero.
 *  c. the given repository does not have core.useReplaceRefs=false.
 */
int replace_refs_enabled(struct repository *r);

/*
 * If object sha1 should be replaced, return the replacement object's
 * name (replaced recursively, if necessary).  The return value is
 * either sha1 or a pointer to a permanently-allocated value.  When
 * object replacement is suppressed, always return sha1.
 *
 * Note: some thread debuggers might point a data race on the
 * replace_map_initialized reading in this function. However, we know there's no
 * problem in the value being updated by one thread right after another one read
 * it here (and it should be written to only once, anyway).
 */
static inline const struct object_id *lookup_replace_object(struct repository *r,
							    const struct object_id *oid)
{
	if (!replace_refs_enabled(r) ||
	    (r->objects->replace_map_initialized &&
	     r->objects->replace_map->map.tablesize == 0))
		return oid;
	return do_lookup_replace_object(r, oid);
}

/*
 * Some commands override config and environment settings for using
 * replace references. Use this method to disable the setting and ensure
 * those other settings will not override this choice. This applies
 * globally to all in-process repositories.
 */
void disable_replace_refs(void);

#endif /* REPLACE_OBJECT_H */
