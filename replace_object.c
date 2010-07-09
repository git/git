#include "cache.h"
#include "sha1-lookup.h"
#include "refs.h"

static struct replace_object {
	unsigned char sha1[2][20];
} **replace_object;

static int replace_object_alloc, replace_object_nr;

static const unsigned char *replace_sha1_access(size_t index, void *table)
{
	struct replace_object **replace = table;
	return replace[index]->sha1[0];
}

static int replace_object_pos(const unsigned char *sha1)
{
	return sha1_pos(sha1, replace_object, replace_object_nr,
			replace_sha1_access);
}

static int register_replace_object(struct replace_object *replace,
				   int ignore_dups)
{
	int pos = replace_object_pos(replace->sha1[0]);

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
	if (replace_object_alloc <= ++replace_object_nr) {
		replace_object_alloc = alloc_nr(replace_object_alloc);
		replace_object = xrealloc(replace_object,
					  sizeof(*replace_object) *
					  replace_object_alloc);
	}
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

	if (strlen(hash) != 40 || get_sha1_hex(hash, repl_obj->sha1[0])) {
		free(repl_obj);
		warning("bad replace ref name: %s", refname);
		return 0;
	}

	/* Copy sha1 from the read ref */
	hashcpy(repl_obj->sha1[1], sha1);

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
}

/* We allow "recursive" replacement. Only within reason, though */
#define MAXREPLACEDEPTH 5

const unsigned char *lookup_replace_object(const unsigned char *sha1)
{
	int pos, depth = MAXREPLACEDEPTH;
	const unsigned char *cur = sha1;

	if (!read_replace_refs)
		return sha1;

	prepare_replace_object();

	/* Try to recursively replace the object */
	do {
		if (--depth < 0)
			die("replace depth too high for object %s",
			    sha1_to_hex(sha1));

		pos = replace_object_pos(cur);
		if (0 <= pos)
			cur = replace_object[pos]->sha1[1];
	} while (0 <= pos);

	return cur;
}
