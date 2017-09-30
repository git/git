#ifndef MRU_H
#define MRU_H

#include "list.h"

/**
 * A simple most-recently-used cache, backed by a doubly-linked list.
 *
 * Usage is roughly:
 *
 *   // Create a list.  Zero-initialization is required.
 *   static struct mru cache;
 *   INIT_LIST_HEAD(&cache.list);
 *
 *   // Add new item to the end of the list.
 *   void *item;
 *   ...
 *   mru_append(&cache, item);
 *
 *   // Mark an item as used, moving it to the front of the list.
 *   mru_mark(&cache, item);
 *
 *   // Reset the list to empty, cleaning up all resources.
 *   mru_clear(&cache);
 *
 * Note that you SHOULD NOT call mru_mark() and then continue traversing the
 * list; it reorders the marked item to the front of the list, and therefore
 * you will begin traversing the whole list again.
 */

struct mru {
	struct list_head list;
	void *item;
};

void mru_append(struct mru *head, void *item);
void mru_mark(struct mru *head, struct mru *entry);
void mru_clear(struct mru *head);

#endif /* MRU_H */
