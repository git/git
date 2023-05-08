/*
 * alloc.c  - specialized allocator for internal objects
 *
 * Copyright (C) 2006 Linus Torvalds
 *
 * The standard malloc/free wastes too much space for objects, partly because
 * it maintains all the allocation infrastructure, but even more because it ends
 * up with maximal alignment because it doesn't know what the object alignment
 * for the new allocation is.
 */
#include "git-compat-util.h"
#include "object.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "repository.h"
#include "tag.h"
#include "alloc.h"

#define BLOCKING 1024

union any_object {
	struct object object;
	struct blob blob;
	struct tree tree;
	struct commit commit;
	struct tag tag;
};

struct alloc_state {
	int nr;    /* number of nodes left in current allocation */
	void *p;   /* first free node in current allocation */

	/* bookkeeping of allocations */
	void **slabs;
	int slab_nr, slab_alloc;
};

struct alloc_state *allocate_alloc_state(void)
{
	return xcalloc(1, sizeof(struct alloc_state));
}

void clear_alloc_state(struct alloc_state *s)
{
	while (s->slab_nr > 0) {
		s->slab_nr--;
		free(s->slabs[s->slab_nr]);
	}

	FREE_AND_NULL(s->slabs);
}

static inline void *alloc_node(struct alloc_state *s, size_t node_size)
{
	void *ret;

	if (!s->nr) {
		s->nr = BLOCKING;
		s->p = xmalloc(BLOCKING * node_size);

		ALLOC_GROW(s->slabs, s->slab_nr + 1, s->slab_alloc);
		s->slabs[s->slab_nr++] = s->p;
	}
	s->nr--;
	ret = s->p;
	s->p = (char *)s->p + node_size;
	memset(ret, 0, node_size);

	return ret;
}

void *alloc_blob_node(struct repository *r)
{
	struct blob *b = alloc_node(r->parsed_objects->blob_state, sizeof(struct blob));
	b->object.type = OBJ_BLOB;
	return b;
}

void *alloc_tree_node(struct repository *r)
{
	struct tree *t = alloc_node(r->parsed_objects->tree_state, sizeof(struct tree));
	t->object.type = OBJ_TREE;
	return t;
}

void *alloc_tag_node(struct repository *r)
{
	struct tag *t = alloc_node(r->parsed_objects->tag_state, sizeof(struct tag));
	t->object.type = OBJ_TAG;
	return t;
}

void *alloc_object_node(struct repository *r)
{
	struct object *obj = alloc_node(r->parsed_objects->object_state, sizeof(union any_object));
	obj->type = OBJ_NONE;
	return obj;
}

/*
 * The returned count is to be used as an index into commit slabs,
 * that are *NOT* maintained per repository, and that is why a single
 * global counter is used.
 */
static unsigned int alloc_commit_index(void)
{
	static unsigned int parsed_commits_count;
	return parsed_commits_count++;
}

void init_commit_node(struct commit *c)
{
	c->object.type = OBJ_COMMIT;
	c->index = alloc_commit_index();
}

void *alloc_commit_node(struct repository *r)
{
	struct commit *c = alloc_node(r->parsed_objects->commit_state, sizeof(struct commit));
	init_commit_node(c);
	return c;
}
