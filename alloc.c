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

#define DEFINE_ALLOCATOR(name)					\
static unsigned int name##_allocs;				\
struct name *alloc_##name##_node(void)				\
{								\
	static int nr;						\
	static struct name *block;				\
								\
	if (!nr) {						\
		nr = BLOCKING;					\
		block = xcalloc(BLOCKING, sizeof(struct name));	\
	}							\
	nr--;							\
	name##_allocs++;					\
	return block++;						\
}

DEFINE_ALLOCATOR(blob)
DEFINE_ALLOCATOR(tree)
DEFINE_ALLOCATOR(commit)
DEFINE_ALLOCATOR(tag)

#ifdef NO_C99_FORMAT
#define SZ_FMT "%u"
#else
#define SZ_FMT "%zu"
#endif

static void report(const char* name, unsigned int count, size_t size)
{
    fprintf(stderr, "%10s: %8u (" SZ_FMT " kB)\n", name, count, size);
}

#undef SZ_FMT

#define REPORT(name)	\
    report(#name, name##_allocs, name##_allocs*sizeof(struct name) >> 10)

void alloc_report(void)
{
	REPORT(blob);
	REPORT(tree);
	REPORT(commit);
	REPORT(tag);
}
