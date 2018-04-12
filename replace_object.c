#include "cache.h"
#include "oidmap.h"
#include "refs.h"
#include "commit.h"

struct replace_object {
	struct oidmap_entry original;
	struct object_id replacement;
};

static struct oidmap replace_map = OIDMAP_INIT;

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
	if (oidmap_put(&replace_map, repl_obj))
		die("duplicate replace ref: %s", refname);

	return 0;
}

static void prepare_replace_object(void)
{
	static int replace_object_prepared;

	if (replace_object_prepared)
		return;

	for_each_replace_ref(register_replace_ref, NULL);
	replace_object_prepared = 1;
	if (!replace_map.map.tablesize)
		check_replace_refs = 0;
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
const struct object_id *do_lookup_replace_object(const struct object_id *oid)
{
	int depth = MAXREPLACEDEPTH;
	const struct object_id *cur = oid;

	prepare_replace_object();

	/* Try to recursively replace the object */
	while (depth-- > 0) {
		struct replace_object *repl_obj = oidmap_get(&replace_map, cur);
		if (!repl_obj)
			return cur;
		cur = &repl_obj->replacement;
	}
	die("replace depth too high for object %s", oid_to_hex(oid));
}
