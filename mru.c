#include "cache.h"
#include "mru.h"

void mru_append(struct mru *mru, void *item)
{
	struct mru_entry *cur = xmalloc(sizeof(*cur));
	cur->item = item;
	cur->prev = mru->tail;
	cur->next = NULL;

	if (mru->tail)
		mru->tail->next = cur;
	else
		mru->head = cur;
	mru->tail = cur;
}

void mru_mark(struct mru *mru, struct mru_entry *entry)
{
	/* If we're already at the front of the list, nothing to do */
	if (mru->head == entry)
		return;

	/* Otherwise, remove us from our current slot... */
	if (entry->prev)
		entry->prev->next = entry->next;
	if (entry->next)
		entry->next->prev = entry->prev;
	else
		mru->tail = entry->prev;

	/* And insert us at the beginning. */
	entry->prev = NULL;
	entry->next = mru->head;
	if (mru->head)
		mru->head->prev = entry;
	mru->head = entry;
}

void mru_clear(struct mru *mru)
{
	struct mru_entry *p = mru->head;

	while (p) {
		struct mru_entry *to_free = p;
		p = p->next;
		free(to_free);
	}
	mru->head = mru->tail = NULL;
}
