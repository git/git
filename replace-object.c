#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
#include "oidmap.h"
#include "odb.h"
#include "replace-object.h"
#include "refs.h"
#include "repository.h"
#include "commit.h"

static int register_replace_ref(const char *refname,
				const char *referent UNUSED,
				const struct object_id *oid,
				int flag UNUSED,
				void *cb_data)
{
	struct repository *r = cb_data;

	/* Get sha1 from refname */
	const char *slash = strrchr(refname, '/');
	const char *hash = slash ? slash + 1 : refname;
	struct replace_object *repl_obj = xmalloc(sizeof(*repl_obj));

	if (get_oid_hex_algop(hash, &repl_obj->original.oid, r->hash_algo)) {
		free(repl_obj);
		warning(_("bad replace ref name: %s"), refname);
		return 0;
	}

	/* Copy sha1 from the read ref */
	oidcpy(&repl_obj->replacement, oid);

	/* Register new object */
	if (oidmap_put(&r->objects->replace_map, repl_obj))
		die(_("duplicate replace ref: %s"), refname);

	return 0;
}

void prepare_replace_object(struct repository *r)
{
	if (r->objects->replace_map_initialized)
		return;

	pthread_mutex_lock(&r->objects->replace_mutex);
	if (r->objects->replace_map_initialized) {
		pthread_mutex_unlock(&r->objects->replace_mutex);
		return;
	}

	oidmap_init(&r->objects->replace_map, 0);

	refs_for_each_replace_ref(get_main_ref_store(r),
				  register_replace_ref, r);
	r->objects->replace_map_initialized = 1;

	pthread_mutex_unlock(&r->objects->replace_mutex);
}

/* We allow "recursive" replacement. Only within reason, though */
#define MAXREPLACEDEPTH 5

/*
 * If a replacement for object oid has been set up, return the
 * replacement object's name (replaced recursively, if necessary).
 * The return value is either oid or a pointer to a
 * permanently-allocated value.  This function always respects replace
 * references, regardless of the value of r->settings.read_replace_refs.
 */
const struct object_id *do_lookup_replace_object(struct repository *r,
						 const struct object_id *oid)
{
	int depth = MAXREPLACEDEPTH;
	const struct object_id *cur = oid;

	prepare_replace_object(r);

	/* Try to recursively replace the object */
	while (depth-- > 0) {
		struct replace_object *repl_obj =
			oidmap_get(&r->objects->replace_map, cur);
		if (!repl_obj)
			return cur;
		cur = &repl_obj->replacement;
	}
	die(_("replace depth too high for object %s"), oid_to_hex(oid));
}

/*
 * This indicator determines whether replace references should be
 * respected process-wide, regardless of which repository is being
 * using at the time.
 */
static int read_replace_refs = 1;

void disable_replace_refs(void)
{
	read_replace_refs = 0;
}

int replace_refs_enabled(struct repository *r)
{
	if (!read_replace_refs)
		return 0;

	if (r->gitdir) {
		prepare_repo_settings(r);
		return r->settings.read_replace_refs;
	}

	/* repository has no objects or refs. */
	return 0;
}
