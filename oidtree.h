#ifndef OIDTREE_H
#define OIDTREE_H

#include "cbtree.h"
#include "hash.h"
#include "mem-pool.h"

/*
 * OID trees are an efficient storage for object IDs that use a critbit tree
 * internally. Common prefixes are duplicated and object IDs are stored in a
 * way that allow easy iteration over the objects in lexicographic order. As a
 * consequence, operations that want to enumerate all object IDs that match a
 * given prefix can be answered efficiently.
 *
 * Note that it is not (yet) possible to store data other than the object IDs
 * themselves in this tree.
 */
struct oidtree {
	struct cb_tree tree;
	struct mem_pool mem_pool;
};

/* Initialize the oidtree so that it is ready for use. */
void oidtree_init(struct oidtree *ot);

/*
 * Release all memory associated with the oidtree and reinitialize it for
 * subsequent use.
 */
void oidtree_clear(struct oidtree *ot);

/*
 * Insert the object ID into the tree and store the given pointer alongside
 * with it. The data pointer of any preexisting entry will be overwritten.
 */
void oidtree_insert(struct oidtree *ot, const struct object_id *oid,
		    void *data);

/* Check whether the tree contains the given object ID. */
bool oidtree_contains(struct oidtree *ot, const struct object_id *oid);

/* Get the payload stored with the given object ID. */
void *oidtree_get(struct oidtree *ot, const struct object_id *oid);

/*
 * Callback function used for `oidtree_each()`. Returning a non-zero exit code
 * will cause iteration to stop. The exit code will be propagated to the caller
 * of `oidtree_each()`.
 */
typedef int (*oidtree_each_cb)(const struct object_id *oid,
			       void *node_data,
			       void *cb_data);

/*
 * Iterate through all object IDs in the tree whose prefix matches the given
 * object ID prefix and invoke the callback function on each of them.
 *
 * Returns any non-zero exit code from the provided callback function.
 */
int oidtree_each(struct oidtree *ot,
		 const struct object_id *prefix, size_t prefix_hex_len,
		 oidtree_each_cb cb, void *cb_data);

#endif /* OIDTREE_H */
