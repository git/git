#include "cache.h"
#include "mru.h"

void mru_append(struct mru *head, void *item)
{
	struct mru *cur = xmalloc(sizeof(*cur));
	cur->item = item;
	list_add_tail(&cur->list, &head->list);
}

void mru_mark(struct mru *head, struct mru *entry)
{
	/* To mark means to put at the front of the list. */
	list_del(&entry->list);
	list_add(&entry->list, &head->list);
}

void mru_clear(struct mru *head)
{
	struct list_head *pos;
	struct list_head *tmp;

	list_for_each_safe(pos, tmp, &head->list) {
		free(list_entry(pos, struct mru, list));
	}
	INIT_LIST_HEAD(&head->list);
}
