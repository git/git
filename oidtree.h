#ifndef OIDTREE_H
#define OIDTREE_H

#include "cbtree.h"
#include "hash.h"
#include "mem-pool.h"

struct oidtree {
	struct cb_tree tree;
	struct mem_pool mem_pool;
};

void oidtree_init(struct oidtree *);
void oidtree_clear(struct oidtree *);
void oidtree_insert(struct oidtree *, const struct object_id *);
int oidtree_contains(struct oidtree *, const struct object_id *);

typedef enum cb_next (*oidtree_iter)(const struct object_id *, void *data);
void oidtree_each(struct oidtree *, const struct object_id *,
			size_t oidhexsz, oidtree_iter, void *data);

#endif /* OIDTREE_H */
