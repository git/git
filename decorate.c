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
	struct object_decoration *hash = n->hash;
	unsigned int j = hash_obj(base, size);

	while (hash[j].base) {
		if (hash[j].base == base) {
			void *old = hash[j].decoration;
			hash[j].decoration = decoration;
			return old;
		}
		if (++j >= size)
			j = 0;
	}
	hash[j].base = base;
	hash[j].decoration = decoration;
	n->nr++;
	return NULL;
}

static void grow_decoration(struct decoration *n)
{
	int i;
	int old_size = n->size;
	struct object_decoration *old_hash = n->hash;

	n->size = (old_size + 1000) * 3 / 2;
	n->hash = xcalloc(n->size, sizeof(struct object_decoration));
	n->nr = 0;

	for (i = 0; i < old_size; i++) {
		const struct object *base = old_hash[i].base;
		void *decoration = old_hash[i].decoration;

		if (!decoration)
			continue;
		insert_decoration(n, base, decoration);
	}
	free(old_hash);
}

/* Add a decoration pointer, return any old one */
void *add_decoration(struct decoration *n, const struct object *obj,
		void *decoration)
{
	int nr = n->nr + 1;

	if (nr > n->size * 2 / 3)
		grow_decoration(n);
	return insert_decoration(n, obj, decoration);
}

/* Lookup a decoration pointer */
void *lookup_decoration(struct decoration *n, const struct object *obj)
{
	unsigned int j;

	/* nothing to lookup */
	if (!n->size)
		return NULL;
	j = hash_obj(obj, n->size);
	for (;;) {
		struct object_decoration *ref = n->hash + j;
		if (ref->base == obj)
			return ref->decoration;
		if (!ref->base)
			return NULL;
		if (++j == n->size)
			j = 0;
	}
}
