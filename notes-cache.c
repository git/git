#include "cache.h"
#include "notes-cache.h"
#include "commit.h"
#include "refs.h"

static int notes_cache_match_validity(const char *ref, const char *validity)
{
	unsigned char sha1[20];
	struct commit *commit;
	struct pretty_print_context pretty_ctx;
	struct strbuf msg = STRBUF_INIT;
	int ret;

	if (read_ref(ref, sha1) < 0)
		return 0;

	commit = lookup_commit_reference_gently(sha1, 1);
	if (!commit)
		return 0;

	memset(&pretty_ctx, 0, sizeof(pretty_ctx));
	format_commit_message(commit, "%s", &msg, &pretty_ctx);
	strbuf_trim(&msg);

	ret = !strcmp(msg.buf, validity);
	strbuf_release(&msg);

	return ret;
}

void notes_cache_init(struct notes_cache *c, const char *name,
		     const char *validity)
{
	struct strbuf ref = STRBUF_INIT;
	int flags = 0;

	memset(c, 0, sizeof(*c));
	c->validity = xstrdup(validity);

	strbuf_addf(&ref, "refs/notes/%s", name);
	if (!notes_cache_match_validity(ref.buf, validity))
		flags = NOTES_INIT_EMPTY;
	init_notes(&c->tree, ref.buf, combine_notes_overwrite, flags);
	strbuf_release(&ref);
}

int notes_cache_write(struct notes_cache *c)
{
	unsigned char tree_sha1[20];
	unsigned char commit_sha1[20];

	if (!c || !c->tree.initialized || !c->tree.ref || !*c->tree.ref)
		return -1;
	if (!c->tree.dirty)
		return 0;

	if (write_notes_tree(&c->tree, tree_sha1))
		return -1;
	if (commit_tree(c->validity, strlen(c->validity), tree_sha1, NULL,
			commit_sha1, NULL, NULL) < 0)
		return -1;
	if (update_ref("update notes cache", c->tree.ref, commit_sha1, NULL,
		       0, UPDATE_REFS_QUIET_ON_ERR) < 0)
		return -1;

	return 0;
}

char *notes_cache_get(struct notes_cache *c, unsigned char key_sha1[20],
		      size_t *outsize)
{
	const unsigned char *value_sha1;
	enum object_type type;
	char *value;
	unsigned long size;

	value_sha1 = get_note(&c->tree, key_sha1);
	if (!value_sha1)
		return NULL;
	value = read_sha1_file(value_sha1, &type, &size);

	*outsize = size;
	return value;
}

int notes_cache_put(struct notes_cache *c, unsigned char key_sha1[20],
		    const char *data, size_t size)
{
	unsigned char value_sha1[20];

	if (write_sha1_file(data, size, "blob", value_sha1) < 0)
		return -1;
	return add_note(&c->tree, key_sha1, value_sha1, NULL);
}
