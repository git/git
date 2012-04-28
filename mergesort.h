#ifndef MERGESORT_H
#define MERGESORT_H

/*
 * Sort linked list in place.
 * - get_next_fn() returns the next element given an element of a linked list.
 * - set_next_fn() takes two elements A and B, and makes B the "next" element
 *   of A on the list.
 * - compare_fn() takes two elements A and B, and returns negative, 0, positive
 *   as the same sign as "subtracting" B from A.
 */
void *llist_mergesort(void *list,
		      void *(*get_next_fn)(const void *),
		      void (*set_next_fn)(void *, void *),
		      int (*compare_fn)(const void *, const void *));

#endif
