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

#endif /* COMMIT_SLAB_HDR_H */
