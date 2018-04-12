#include "cache.h"
#include "oidmap.h"
#include "object-store.h"
#include "replace-object.h"
#include "refs.h"
#include "repository.h"
#include "commit.h"

static int register_replace_ref(const char *refname,
				const struct object_id *oid,
				int flag, void *cb_data)
{
	/* Get sha1 from refname */
	const char *slash = strrchr(refname, '/');
	const char *hash = slash ? slash + 1 : refname;
	struct replace_object *repl_obj = xmalloc(sizeof(*repl_obj));

	if (get_oid_hex(hash, &repl_obj->original.oid)) {
		free(repl_obj);
		warning("bad replace ref name: %s", refname);
		return 0;
	}

	/* Copy sha1 from the read ref */
	oidcpy(&repl_obj->replacement, oid);

	/* Register new object */
	if (oidmap_put(the_repository->objects->replace_map, repl_obj))
		die("duplicate replace ref: %s", refname);

	return 0;
}

#define prepare_replace_object(r) \
	prepare_replace_object_##r()
static void prepare_replace_object_the_repository(void)
{
	if (the_repository->objects->replace_map)
		return;

	the_repository->objects->replace_map =
		xmalloc(sizeof(*the_repository->objects->replace_map));
	oidmap_init(the_repository->objects->replace_map, 0);

	for_each_replace_ref(the_repository, register_replace_ref, NULL);
}

/* We allow "recursive" replacement. Only within reason, though */
#define MAXREPLACEDEPTH 5

/*
 * If a replacement for object oid has been set up, return the
 * replacement object's name (replaced recursively, if necessary).
 * The return value is either oid or a pointer to a
 * permanently-allocated value.  This function always respects replace
 * references, regardless of the value of check_replace_refs.
 */
const struct object_id *do_lookup_replace_object_the_repository(const struct object_id *oid)
{
	int depth = MAXREPLACEDEPTH;
	const struct object_id *cur = oid;

	prepare_replace_object(the_repository);

	/* Try to recursively replace the object */
	while (depth-- > 0) {
		struct replace_object *repl_obj =
			oidmap_get(the_repository->objects->replace_map, cur);
		if (!repl_obj)
			return cur;
		cur = &repl_obj->replacement;
	}
	die("replace depth too high for object %s", oid_to_hex(oid));
}
