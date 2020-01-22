#include "cache.h"
#include "notes-cache.h"
#include "object-store.h"
#include "repository.h"
#include "commit.h"
#include "refs.h"

static int notes_cache_match_validity(struct repository *r,
				      const char *ref,
				      const char *validity)
{
	struct object_id oid;
	struct commit *commit;
	struct pretty_print_context pretty_ctx;
	struct strbuf msg = STRBUF_INIT;
	int ret;

	if (read_ref(ref, &oid) < 0)
		return 0;

	commit = lookup_commit_reference_gently(r, &oid, 1);
	if (!commit)
		return 0;

	memset(&pretty_ctx, 0, sizeof(pretty_ctx));
	format_commit_message(commit, "%s", &msg, &pretty_ctx);
	strbuf_trim(&msg);

	ret = !strcmp(msg.buf, validity);
	strbuf_release(&msg);

	return ret;
}

void notes_cache_init(struct repository *r, struct notes_cache *c,
		      const char *name, const char *validity)
{
	struct strbuf ref = STRBUF_INIT;
	int flags = NOTES_INIT_WRITABLE;

	memset(c, 0, sizeof(*c));
	c->validity = xstrdup(validity);

	strbuf_addf(&ref, "refs/notes/%s", name);
	if (!notes_cache_match_validity(r, ref.buf, validity))
		flags |= NOTES_INIT_EMPTY;
	init_notes(&c->tree, ref.buf, combine_notes_overwrite, flags);
	strbuf_release(&ref);
}

int notes_cache_write(struct notes_cache *c)
{
	struct object_id tree_oid, commit_oid;

	if (!c || !c->tree.initialized || !c->tree.update_ref ||
	    !*c->tree.update_ref)
		return -1;
	if (!c->tree.dirty)
		return 0;

	if (write_notes_tree(&c->tree, &tree_oid))
		return -1;
	if (commit_tree(c->validity, strlen(c->validity), &tree_oid, NULL,
			&commit_oid, NULL, NULL) < 0)
		return -1;
	if (update_ref("update notes cache", c->tree.update_ref, &commit_oid,
		       NULL, 0, UPDATE_REFS_QUIET_ON_ERR) < 0)
		return -1;

	return 0;
}

char *notes_cache_get(struct notes_cache *c, struct object_id *key_oid,
		      size_t *outsize)
{
	const struct object_id *value_oid;
	enum object_type type;
	char *value;
	unsigned long size;

	value_oid = get_note(&c->tree, key_oid);
	if (!value_oid)
		return NULL;
	value = read_object_file(value_oid, &type, &size);

	*outsize = size;
	return value;
}

int notes_cache_put(struct notes_cache *c, struct object_id *key_oid,
		    const char *data, size_t size)
{
	struct object_id value_oid;

	if (write_object_file(data, size, "blob", &value_oid) < 0)
		return -1;
	return add_note(&c->tree, key_oid, &value_oid, NULL);
}
