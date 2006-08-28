#include "cache.h"
#include "object.h"

int track_object_refs = 0;

static unsigned int refs_hash_size, nr_object_refs;
static struct object_refs **refs_hash;

static unsigned int hash_obj(struct object *obj, unsigned int n)
{
	unsigned int hash = *(unsigned int *)obj->sha1;
	return hash % n;
}

static void insert_ref_hash(struct object_refs *ref, struct object_refs **hash, unsigned int size)
{
	int j = hash_obj(ref->base, size);

	while (hash[j]) {
		j++;
		if (j >= size)
			j = 0;
	}
	hash[j] = ref;
}

static void grow_refs_hash(void)
{
	int i;
	int new_hash_size = (refs_hash_size + 1000) * 3 / 2;
	struct object_refs **new_hash;

	new_hash = xcalloc(new_hash_size, sizeof(struct object_refs *));
	for (i = 0; i < refs_hash_size; i++) {
		struct object_refs *ref = refs_hash[i];
		if (!ref)
			continue;
		insert_ref_hash(ref, new_hash, new_hash_size);
	}
	free(refs_hash);
	refs_hash = new_hash;
	refs_hash_size = new_hash_size;
}

static void add_object_refs(struct object *obj, struct object_refs *ref)
{
	int nr = nr_object_refs + 1;

	if (nr > refs_hash_size * 2 / 3)
		grow_refs_hash();
	ref->base = obj;
	insert_ref_hash(ref, refs_hash, refs_hash_size);
	nr_object_refs = nr;
}

struct object_refs *lookup_object_refs(struct object *obj)
{
	int j = hash_obj(obj, refs_hash_size);
	struct object_refs *ref;

	while ((ref = refs_hash[j]) != NULL) {
		if (ref->base == obj)
			break;
		j++;
		if (j >= refs_hash_size)
			j = 0;
	}
	return ref;
}

struct object_refs *alloc_object_refs(unsigned count)
{
	struct object_refs *refs;
	size_t size = sizeof(*refs) + count*sizeof(struct object *);

	refs = xcalloc(1, size);
	refs->count = count;
	return refs;
}

static int compare_object_pointers(const void *a, const void *b)
{
	const struct object * const *pa = a;
	const struct object * const *pb = b;
	if (*pa == *pb)
		return 0;
	else if (*pa < *pb)
		return -1;
	else
		return 1;
}

void set_object_refs(struct object *obj, struct object_refs *refs)
{
	unsigned int i, j;

	/* Do not install empty list of references */
	if (refs->count < 1) {
		free(refs);
		return;
	}

	/* Sort the list and filter out duplicates */
	qsort(refs->ref, refs->count, sizeof(refs->ref[0]),
	      compare_object_pointers);
	for (i = j = 1; i < refs->count; i++) {
		if (refs->ref[i] != refs->ref[i - 1])
			refs->ref[j++] = refs->ref[i];
	}
	if (j < refs->count) {
		/* Duplicates were found - reallocate list */
		size_t size = sizeof(*refs) + j*sizeof(struct object *);
		refs->count = j;
		refs = xrealloc(refs, size);
	}

	for (i = 0; i < refs->count; i++)
		refs->ref[i]->used = 1;
	add_object_refs(obj, refs);
}

void mark_reachable(struct object *obj, unsigned int mask)
{
	const struct object_refs *refs;

	if (!track_object_refs)
		die("cannot do reachability with object refs turned off");
	/* nothing to lookup */
	if (!refs_hash_size)
		return;
	/* If we've been here already, don't bother */
	if (obj->flags & mask)
		return;
	obj->flags |= mask;
	refs = lookup_object_refs(obj);
	if (refs) {
		unsigned i;
		for (i = 0; i < refs->count; i++)
			mark_reachable(refs->ref[i], mask);
	}
}


