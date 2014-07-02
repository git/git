#ifndef COMMIT_SLAB_H
#define COMMIT_SLAB_H

/*
 * define_commit_slab(slabname, elemtype) creates boilerplate code to define
 * a new struct (struct slabname) that is used to associate a piece of data
 * of elemtype to commits, and a few functions to use that struct.
 *
 * After including this header file, using:
 *
 * define_commit_slab(indegee, int);
 *
 * will let you call the following functions:
 *
 * - int *indegree_at(struct indegree *, struct commit *);
 *
 *   This function locates the data associated with the given commit in
 *   the indegree slab, and returns the pointer to it.
 *
 * - void init_indegree(struct indegree *);
 *   void init_indegree_with_stride(struct indegree *, int);
 *
 *   Initializes the indegree slab that associates an array of integers
 *   to each commit. 'stride' specifies how big each array is.  The slab
 *   that is initialized by the variant without "_with_stride" associates
 *   each commit with an array of one integer.
 *
 * - void clear_indegree(struct indegree *);
 *
 *   Empties the slab.  The slab can be reused with the same stride
 *   without calling init_indegree() again or can be reconfigured to a
 *   different stride by calling init_indegree_with_stride().
 *
 *   Call this function before the slab falls out of scope to avoid
 *   leaking memory.
 */

/* allocate ~512kB at once, allowing for malloc overhead */
#ifndef COMMIT_SLAB_SIZE
#define COMMIT_SLAB_SIZE (512*1024-32)
#endif

#define MAYBE_UNUSED __attribute__((__unused__))

#define define_commit_slab(slabname, elemtype) 				\
									\
struct slabname {							\
	unsigned slab_size;						\
	unsigned stride;						\
	unsigned slab_count;						\
	elemtype **slab;						\
};									\
static int stat_ ##slabname## realloc;					\
									\
static MAYBE_UNUSED void init_ ##slabname## _with_stride(struct slabname *s, \
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
static MAYBE_UNUSED void init_ ##slabname(struct slabname *s)		\
{									\
	init_ ##slabname## _with_stride(s, 1);				\
}									\
									\
static MAYBE_UNUSED void clear_ ##slabname(struct slabname *s)		\
{									\
	int i;								\
	for (i = 0; i < s->slab_count; i++)				\
		free(s->slab[i]);					\
	s->slab_count = 0;						\
	free(s->slab);							\
	s->slab = NULL;							\
}									\
									\
static MAYBE_UNUSED elemtype *slabname## _at(struct slabname *s,	\
				       const struct commit *c)		\
{									\
	int nth_slab, nth_slot;						\
									\
	nth_slab = c->index / s->slab_size;				\
	nth_slot = c->index % s->slab_size;				\
									\
	if (s->slab_count <= nth_slab) {				\
		int i;							\
		s->slab = xrealloc(s->slab,				\
				   (nth_slab + 1) * sizeof(*s->slab));	\
		stat_ ##slabname## realloc++;				\
		for (i = s->slab_count; i <= nth_slab; i++)		\
			s->slab[i] = NULL;				\
		s->slab_count = nth_slab + 1;				\
	}								\
	if (!s->slab[nth_slab])						\
		s->slab[nth_slab] = xcalloc(s->slab_size,		\
					    sizeof(**s->slab) * s->stride);		\
	return &s->slab[nth_slab][nth_slot * s->stride];				\
}									\
									\
static int stat_ ##slabname## realloc

/*
 * Note that this seemingly redundant second declaration is required
 * to allow a terminating semicolon, which makes instantiations look
 * like function declarations.  I.e., the expansion of
 *
 *    define_commit_slab(indegree, int);
 *
 * ends in 'static int stat_indegreerealloc;'.  This would otherwise
 * be a syntax error according (at least) to ISO C.  It's hard to
 * catch because GCC silently parses it by default.
 */

/*
 * Statically initialize a commit slab named "var". Note that this
 * evaluates "stride" multiple times! Example:
 *
 *   struct indegree indegrees = COMMIT_SLAB_INIT(1, indegrees);
 *
 */
#define COMMIT_SLAB_INIT(stride, var) { \
	COMMIT_SLAB_SIZE / sizeof(**((var).slab)) / (stride), \
	(stride), 0, NULL \
}

#endif /* COMMIT_SLAB_H */
