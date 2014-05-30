#include "cache.h"
#include "sha1-lookup.h"
#include "refs.h"
#include "commit.h"

/*
 * An array of replacements.  The array is kept sorted by the original
 * sha1.
 */
static struct replace_object {
	unsigned char original[20];
	unsigned char replacement[20];
} **replace_object;

static int replace_object_alloc, replace_object_nr;

static const unsigned char *replace_sha1_access(size_t index, void *table)
{
	struct replace_object **replace = table;
	return replace[index]->original;
}

static int replace_object_pos(const unsigned char *sha1)
{
	return sha1_pos(sha1, replace_object, replace_object_nr,
			replace_sha1_access);
}

static int register_replace_object(struct replace_object *replace,
				   int ignore_dups)
{
	int pos = replace_object_pos(replace->original);

	if (0 <= pos) {
		if (ignore_dups)
			free(replace);
		else {
			free(replace_object[pos]);
			replace_object[pos] = replace;
		}
		return 1;
	}
	pos = -pos - 1;
	ALLOC_GROW(replace_object, replace_object_nr + 1, replace_object_alloc);
	replace_object_nr++;
	if (pos < replace_object_nr)
		memmove(replace_object + pos + 1,
			replace_object + pos,
			(replace_object_nr - pos - 1) *
			sizeof(*replace_object));
	replace_object[pos] = replace;
	return 0;
}

static int register_replace_ref(const char *refname,
				const unsigned char *sha1,
				int flag, void *cb_data)
{
	/* Get sha1 from refname */
	const char *slash = strrchr(refname, '/');
	const char *hash = slash ? slash + 1 : refname;
	struct replace_object *repl_obj = xmalloc(sizeof(*repl_obj));

	if (strlen(hash) != 40 || get_sha1_hex(hash, repl_obj->original)) {
		free(repl_obj);
		warning("bad replace ref name: %s", refname);
		return 0;
	}

	/* Copy sha1 from the read ref */
	hashcpy(repl_obj->replacement, sha1);

	/* Register new object */
	if (register_replace_object(repl_obj, 1))
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
	if (!replace_object_nr)
		check_replace_refs = 0;
}

/* We allow "recursive" replacement. Only within reason, though */
#define MAXREPLACEDEPTH 5

/*
 * If a replacement for object sha1 has been set up, return the
 * replacement object's name (replaced recursively, if necessary).
 * The return value is either sha1 or a pointer to a
 * permanently-allocated value.  This function always respects replace
 * references, regardless of the value of check_replace_refs.
 */
const unsigned char *do_lookup_replace_object(const unsigned char *sha1)
{
	int pos, depth = MAXREPLACEDEPTH;
	const unsigned char *cur = sha1;

	prepare_replace_object();

	/* Try to recursively replace the object */
	do {
		if (--depth < 0)
			die("replace depth too high for object %s",
			    sha1_to_hex(sha1));

		pos = replace_object_pos(cur);
		if (0 <= pos)
			cur = replace_object[pos]->replacement;
	} while (0 <= pos);

	return cur;
}
