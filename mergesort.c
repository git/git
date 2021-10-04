#include "cache.h"
#include "mergesort.h"

/* Combine two sorted lists.  Take from `list` on equality. */
static void *llist_merge(void *list, void *other,
			 void *(*get_next_fn)(const void *),
			 void (*set_next_fn)(void *, void *),
			 int (*compare_fn)(const void *, const void *))
{
	void *result = list, *tail;

	if (compare_fn(list, other) > 0) {
		result = other;
		goto other;
	}
	for (;;) {
		do {
			tail = list;
			list = get_next_fn(list);
			if (!list) {
				set_next_fn(tail, other);
				return result;
			}
		} while (compare_fn(list, other) <= 0);
		set_next_fn(tail, other);
	other:
		do {
			tail = other;
			other = get_next_fn(other);
			if (!other) {
				set_next_fn(tail, list);
				return result;
			}
		} while (compare_fn(list, other) > 0);
		set_next_fn(tail, list);
	}
}

/*
 * Perform an iterative mergesort using an array of sublists.
 *
 * n is the number of items.
 * ranks[i] is undefined if n & 2^i == 0, and assumed empty.
 * ranks[i] contains a sublist of length 2^i otherwise.
 *
 * The number of bits in a void pointer limits the number of objects
 * that can be created, and thus the number of array elements necessary
 * to be able to sort any valid list.
 *
 * Adding an item to this array is like incrementing a binary number;
 * positional values for set bits correspond to sublist lengths.
 */
void *llist_mergesort(void *list,
		      void *(*get_next_fn)(const void *),
		      void (*set_next_fn)(void *, void *),
		      int (*compare_fn)(const void *, const void *))
{
	void *ranks[bitsizeof(void *)];
	size_t n = 0;
	int i;

	while (list) {
		void *next = get_next_fn(list);
		if (next)
			set_next_fn(list, NULL);
		for (i = 0; n & (1 << i); i++)
			list = llist_merge(ranks[i], list, get_next_fn,
					   set_next_fn, compare_fn);
		n++;
		ranks[i] = list;
		list = next;
	}

	for (i = 0; n; i++, n >>= 1) {
		if (!(n & 1))
			continue;
		if (list)
			list = llist_merge(ranks[i], list, get_next_fn,
					   set_next_fn, compare_fn);
		else
			list = ranks[i];
	}
	return list;
}
