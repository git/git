/*
 * Copyright (C) 2002 Free Software Foundation, Inc.
 * (originally part of the GNU C Library and Userspace RCU)
 * Contributed by Ulrich Drepper <drepper@redhat.com>, 2002.
 *
 * Copyright (C) 2009 Pierre-Marc Fournier
 * Conversion to RCU list.
 * Copyright (C) 2010 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef LIST_H
#define LIST_H	1

/*
 * The definitions of this file are adopted from those which can be
 * found in the Linux kernel headers to enable people familiar with the
 * latter find their way in these sources as well.
 */

/* Basic type for the double-link list. */
struct list_head {
	struct list_head *next, *prev;
};

/* avoid conflicts with BSD-only sys/queue.h */
#undef LIST_HEAD
/* Define a variable with the head and tail of the list. */
#define LIST_HEAD(name) \
	struct list_head name = { &(name), &(name) }

/* Initialize a new list head. */
#define INIT_LIST_HEAD(ptr) \
	(ptr)->next = (ptr)->prev = (ptr)

#define LIST_HEAD_INIT(name) { &(name), &(name) }

/* Add new element at the head of the list. */
static inline void list_add(struct list_head *newp, struct list_head *head)
{
	head->next->prev = newp;
	newp->next = head->next;
	newp->prev = head;
	head->next = newp;
}

/* Add new element at the tail of the list. */
static inline void list_add_tail(struct list_head *newp, struct list_head *head)
{
	head->prev->next = newp;
	newp->next = head;
	newp->prev = head->prev;
	head->prev = newp;
}

/* Remove element from list. */
static inline void __list_del(struct list_head *prev, struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

/* Remove element from list. */
static inline void list_del(struct list_head *elem)
{
	__list_del(elem->prev, elem->next);
}

/* Remove element from list, initializing the element's list pointers. */
static inline void list_del_init(struct list_head *elem)
{
	list_del(elem);
	INIT_LIST_HEAD(elem);
}

/* Delete from list, add to another list as head. */
static inline void list_move(struct list_head *elem, struct list_head *head)
{
	__list_del(elem->prev, elem->next);
	list_add(elem, head);
}

/* Replace an old entry. */
static inline void list_replace(struct list_head *old, struct list_head *newp)
{
	newp->next = old->next;
	newp->prev = old->prev;
	newp->prev->next = newp;
	newp->next->prev = newp;
}

/* Join two lists. */
static inline void list_splice(struct list_head *add, struct list_head *head)
{
	/* Do nothing if the list which gets added is empty. */
	if (add != add->next) {
		add->next->prev = head;
		add->prev->next = head->next;
		head->next->prev = add->prev;
		head->next = add->next;
	}
}

/* Get typed element from list at a given position. */
#define list_entry(ptr, type, member) \
	((type *) ((char *) (ptr) - offsetof(type, member)))

/* Get first entry from a list. */
#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

/* Iterate forward over the elements of the list. */
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

/*
 * Iterate forward over the elements list. The list elements can be
 * removed from the list while doing this.
 */
#define list_for_each_safe(pos, p, head) \
	for (pos = (head)->next, p = pos->next; \
		pos != (head); \
		pos = p, p = pos->next)

/* Iterate backward over the elements of the list. */
#define list_for_each_prev(pos, head) \
	for (pos = (head)->prev; pos != (head); pos = pos->prev)

/*
 * Iterate backwards over the elements list. The list elements can be
 * removed from the list while doing this.
 */
#define list_for_each_prev_safe(pos, p, head) \
	for (pos = (head)->prev, p = pos->prev; \
		pos != (head); \
		pos = p, p = pos->prev)

static inline int list_empty(struct list_head *head)
{
	return head == head->next;
}

static inline void list_replace_init(struct list_head *old,
				     struct list_head *newp)
{
	struct list_head *head = old->next;

	list_del(old);
	list_add_tail(newp, head);
	INIT_LIST_HEAD(old);
}

#endif /* LIST_H */
