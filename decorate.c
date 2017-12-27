/*
 * decorate.c - decorate a git object with some arbitrary
 * data.
 */
#include "cache.h"
#include "object.h"
#include "decorate.h"

static unsigned int hash_obj(const struct object *obj, unsigned int n)
{
	return sha1hash(obj->oid.hash) % n;
}

static void *insert_decoration(struct decoration *n, const struct object *base, void *decoration)
{
	int size = n->size;
	struct decoration_entry *entries = n->entries;
	unsigned int j = hash_obj(base, size);

	while (entries[j].base) {
		if (entries[j].base == base) {
			void *old = entries[j].decoration;
			entries[j].decoration = decoration;
			return old;
		}
		if (++j >= size)
			j = 0;
	}
	entries[j].base = base;
	entries[j].decoration = decoration;
	n->nr++;
	return NULL;
}

static void grow_decoration(struct decoration *n)
{
	int i;
	int old_size = n->size;
	struct decoration_entry *old_entries = n->entries;

	n->size = (old_size + 1000) * 3 / 2;
	n->entries = xcalloc(n->size, sizeof(struct decoration_entry));
	n->nr = 0;

	for (i = 0; i < old_size; i++) {
		const struct object *base = old_entries[i].base;
		void *decoration = old_entries[i].decoration;

		if (!decoration)
			continue;
		insert_decoration(n, base, decoration);
	}
	free(old_entries);
}

void *add_decoration(struct decoration *n, const struct object *obj,
		void *decoration)
{
	int nr = n->nr + 1;

	if (nr > n->size * 2 / 3)
		grow_decoration(n);
	return insert_decoration(n, obj, decoration);
}

void *lookup_decoration(struct decoration *n, const struct object *obj)
{
	unsigned int j;

	/* nothing to lookup */
	if (!n->size)
		return NULL;
	j = hash_obj(obj, n->size);
	for (;;) {
		struct decoration_entry *ref = n->entries + j;
		if (ref->base == obj)
			return ref->decoration;
		if (!ref->base)
			return NULL;
		if (++j == n->size)
			j = 0;
	}
}
