#include "cache.h"
#include "object.h"
#include "decorate.h"

int track_object_refs = 0;

static struct decoration ref_decorate;

struct object_refs *lookup_object_refs(struct object *base)
{
	return lookup_decoration(&ref_decorate, base);
}

static void add_object_refs(struct object *obj, struct object_refs *refs)
{
	if (add_decoration(&ref_decorate, obj, refs))
		die("object %s tried to add refs twice!", sha1_to_hex(obj->sha1));
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


