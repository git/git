#ifndef MRU_H
#define MRU_H

/**
 * A simple most-recently-used cache, backed by a doubly-linked list.
 *
 * Usage is roughly:
 *
 *   // Create a list.  Zero-initialization is required.
 *   static struct mru cache;
 *   mru_append(&cache, item);
 *   ...
 *
 *   // Iterate in MRU order.
 *   struct mru_entry *p;
 *   for (p = cache.head; p; p = p->next) {
 *	if (matches(p->item))
 *		break;
 *   }
 *
 *   // Mark an item as used, moving it to the front of the list.
 *   mru_mark(&cache, p);
 *
 *   // Reset the list to empty, cleaning up all resources.
 *   mru_clear(&cache);
 *
 * Note that you SHOULD NOT call mru_mark() and then continue traversing the
 * list; it reorders the marked item to the front of the list, and therefore
 * you will begin traversing the whole list again.
 */

struct mru_entry {
	void *item;
	struct mru_entry *prev, *next;
};

struct mru {
	struct mru_entry *head, *tail;
};

void mru_append(struct mru *mru, void *item);
void mru_mark(struct mru *mru, struct mru_entry *entry);
void mru_clear(struct mru *mru);

#endif /* MRU_H */
