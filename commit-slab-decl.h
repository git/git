#ifndef COMMIT_SLAB_HDR_H
#define COMMIT_SLAB_HDR_H

/* allocate ~512kB at once, allowing for malloc overhead */
#ifndef COMMIT_SLAB_SIZE
#define COMMIT_SLAB_SIZE (512*1024-32)
#endif

#define declare_commit_slab(slabname, elemtype) 			\
									\
struct slabname {							\
	unsigned slab_size;						\
	unsigned stride;						\
	unsigned slab_count;						\
	elemtype **slab;						\
}

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

#define declare_commit_slab_prototypes(slabname, elemtype)		\
									\
void init_ ##slabname## _with_stride(struct slabname *s, unsigned stride); \
void init_ ##slabname(struct slabname *s);				\
void clear_ ##slabname(struct slabname *s);				\
elemtype *slabname## _at_peek(struct slabname *s, const struct commit *c, int add_if_missing); \
elemtype *slabname## _at(struct slabname *s, const struct commit *c);	\
elemtype *slabname## _peek(struct slabname *s, const struct commit *c)

#define define_shared_commit_slab(slabname, elemtype) \
	declare_commit_slab(slabname, elemtype); \
	declare_commit_slab_prototypes(slabname, elemtype)

#endif /* COMMIT_SLAB_HDR_H */
