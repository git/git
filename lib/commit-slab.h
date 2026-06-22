#ifndef COMMIT_SLAB_H
#define COMMIT_SLAB_H

#include "commit-slab-decl.h"
#include "commit-slab-impl.h"

/*
 * define_commit_slab(slabname, elemtype) creates boilerplate code to define
 * a new struct (struct slabname) that is used to associate a piece of data
 * of elemtype to commits, and a few functions to use that struct.
 *
 * After including this header file, using:
 *
 * define_commit_slab(indegree, int);
 *
 * will let you call the following functions:
 *
 * - int *indegree_at(struct indegree *, struct commit *);
 *
 *   This function locates the data associated with the given commit in
 *   the indegree slab, and returns the pointer to it.  The location to
 *   store the data is allocated as necessary.
 *
 * - int *indegree_peek(struct indegree *, struct commit *);
 *
 *   This function is similar to indegree_at(), but it will return NULL
 *   if the location to store the data associated with the given commit
 *   has not been allocated yet.
 *   Note that the location to store the data might have already been
 *   allocated even if no indegree_at() call has been made for that commit
 *   yet; in this case this function returns a pointer to a
 *   zero-initialized location.
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
 *
 * - void deep_clear_indegree(struct indegree *, void (*free_fn)(int*))
 *
 *   Empties the slab, similar to clear_indegree(), but in addition it
 *   calls the given 'free_fn' for each slab entry to release any
 *   additional memory that might be owned by the entry (but not the
 *   entry itself!).
 *   Note that 'free_fn' might be called even for entries for which no
 *   indegree_at() call has been made; in this case 'free_fn' is invoked
 *   with a pointer to a zero-initialized location.
 */

#define define_commit_slab(slabname, elemtype) \
	declare_commit_slab(slabname, elemtype); \
	implement_static_commit_slab(slabname, elemtype)

#endif /* COMMIT_SLAB_H */
