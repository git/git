/*
 * alloc.c  - specialized allocator for internal objects
 *
 * Copyright (C) 2006 Linus Torvalds
 *
 * The standard malloc/free wastes too much space for objects, partly because
 * it maintains all the allocation infrastructure (which isn't needed, since
 * we never free an object descriptor anyway), but even more because it ends
 * up with maximal alignment because it doesn't know what the object alignment
 * for the new allocation is.
 */
#include "cache.h"
#include "object.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "tag.h"

#define BLOCKING 1024

#define DEFINE_ALLOCATOR(name, type)				\
static struct alloc_state name##_state;				\
void *alloc_##name##_node(void)					\
{								\
	return alloc_node(&name##_state, sizeof(type));		\
}

union any_object {
	struct object object;
	struct blob blob;
	struct tree tree;
	struct commit commit;
	struct tag tag;
};

struct alloc_state {
	int count; /* total number of nodes allocated */
	int nr;    /* number of nodes left in current allocation */
	void *p;   /* first free node in current allocation */
};

static inline void *alloc_node(struct alloc_state *s, size_t node_size)
{
	void *ret;

	if (!s->nr) {
		s->nr = BLOCKING;
		s->p = xmalloc(BLOCKING * node_size);
	}
	s->nr--;
	s->count++;
	ret = s->p;
	s->p = (char *)s->p + node_size;
	memset(ret, 0, node_size);
	return ret;
}

DEFINE_ALLOCATOR(blob, struct blob)
DEFINE_ALLOCATOR(tree, struct tree)
DEFINE_ALLOCATOR(tag, struct tag)
DEFINE_ALLOCATOR(object, union any_object)

static struct alloc_state commit_state;

void *alloc_commit_node(void)
{
	static int commit_count;
	struct commit *c = alloc_node(&commit_state, sizeof(struct commit));
	c->index = commit_count++;
	return c;
}

static void report(const char *name, unsigned int count, size_t size)
{
	fprintf(stderr, "%10s: %8u (%"PRIuMAX" kB)\n",
			name, count, (uintmax_t) size);
}

#define REPORT(name, type)	\
    report(#name, name##_state.count, name##_state.count * sizeof(type) >> 10)

void alloc_report(void)
{
	REPORT(blob, struct blob);
	REPORT(tree, struct tree);
	REPORT(commit, struct commit);
	REPORT(tag, struct tag);
	REPORT(object, union any_object);
}
