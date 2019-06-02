#ifndef COMMIT_SLAB_IMPL_H
#define COMMIT_SLAB_IMPL_H

#include "git-compat-util.h"

#define implement_static_commit_slab(slabname, elemtype) \
	implement_commit_slab(slabname, elemtype, MAYBE_UNUSED static)

#define implement_shared_commit_slab(slabname, elemtype) \
	implement_commit_slab(slabname, elemtype, )

#define implement_commit_slab(slabname, elemtype, scope)		\
									\
scope void init_ ##slabname## _with_stride(struct slabname *s,		\
						   unsigned stride)	\
{									\
	unsigned int elem_size;						\
	if (!stride)							\
		stride = 1;						\
	s->stride = stride;						\
	elem_size = sizeof(elemtype) * stride;				\
	s->slab_size = COMMIT_SLAB_SIZE / elem_size;			\
	s->slab_count = 0;						\
	s->slab = NULL;							\
}									\
									\
scope void init_ ##slabname(struct slabname *s)				\
{									\
	init_ ##slabname## _with_stride(s, 1);				\
}									\
									\
scope void clear_ ##slabname(struct slabname *s)			\
{									\
	unsigned int i;							\
	for (i = 0; i < s->slab_count; i++)				\
		free(s->slab[i]);					\
	s->slab_count = 0;						\
	FREE_AND_NULL(s->slab);						\
}									\
									\
scope elemtype *slabname## _at_peek(struct slabname *s,			\
						  const struct commit *c, \
						  int add_if_missing)   \
{									\
	unsigned int nth_slab, nth_slot;				\
									\
	nth_slab = c->index / s->slab_size;				\
	nth_slot = c->index % s->slab_size;				\
									\
	if (s->slab_count <= nth_slab) {				\
		unsigned int i;						\
		if (!add_if_missing)					\
			return NULL;					\
		REALLOC_ARRAY(s->slab, nth_slab + 1);			\
		for (i = s->slab_count; i <= nth_slab; i++)		\
			s->slab[i] = NULL;				\
		s->slab_count = nth_slab + 1;				\
	}								\
	if (!s->slab[nth_slab]) {					\
		if (!add_if_missing)					\
			return NULL;					\
		s->slab[nth_slab] = xcalloc(s->slab_size,		\
					    sizeof(**s->slab) * s->stride);		\
	}								\
	return &s->slab[nth_slab][nth_slot * s->stride];		\
}									\
									\
scope elemtype *slabname## _at(struct slabname *s,			\
					     const struct commit *c)	\
{									\
	return slabname##_at_peek(s, c, 1);				\
}									\
									\
scope elemtype *slabname## _peek(struct slabname *s,			\
					     const struct commit *c)	\
{									\
	return slabname##_at_peek(s, c, 0);				\
}									\
									\
struct slabname

/*
 * Note that this redundant forward declaration is required
 * to allow a terminating semicolon, which makes instantiations look
 * like function declarations.  I.e., the expansion of
 *
 *    implement_commit_slab(indegree, int, static);
 *
 * ends in 'struct indegree;'.  This would otherwise
 * be a syntax error according (at least) to ISO C.  It's hard to
 * catch because GCC silently parses it by default.
 */

#endif	/* COMMIT_SLAB_IMPL_H */
