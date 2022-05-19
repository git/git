#ifndef cummit_SLAB_DECL_H
#define cummit_SLAB_DECL_H

/* allocate ~512kB at once, allowing for malloc overhead */
#ifndef cummit_SLAB_SIZE
#define cummit_SLAB_SIZE (512*1024-32)
#endif

#define declare_cummit_slab(slabname, elemtype) 			\
									\
struct slabname {							\
	unsigned slab_size;						\
	unsigned stride;						\
	unsigned slab_count;						\
	elemtype **slab;						\
}

/*
 * Statically initialize a cummit slab named "var". Note that this
 * evaluates "stride" multiple times! Example:
 *
 *   struct indegree indegrees = cummit_SLAB_INIT(1, indegrees);
 *
 */
#define cummit_SLAB_INIT(stride, var) { \
	cummit_SLAB_SIZE / sizeof(**((var).slab)) / (stride), \
	(stride), 0, NULL \
}

#define declare_cummit_slab_prototypes(slabname, elemtype)		\
									\
void init_ ##slabname## _with_stride(struct slabname *s, unsigned stride); \
void init_ ##slabname(struct slabname *s);				\
void clear_ ##slabname(struct slabname *s);				\
void deep_clear_ ##slabname(struct slabname *s, void (*free_fn)(elemtype *ptr)); \
elemtype *slabname## _at_peek(struct slabname *s, const struct cummit *c, int add_if_missing); \
elemtype *slabname## _at(struct slabname *s, const struct cummit *c);	\
elemtype *slabname## _peek(struct slabname *s, const struct cummit *c)

#define define_shared_cummit_slab(slabname, elemtype) \
	declare_cummit_slab(slabname, elemtype); \
	declare_cummit_slab_prototypes(slabname, elemtype)

#endif /* cummit_SLAB_DECL_H */
