#include "cache.h"
#include "mergesort.h"

/* Combine two sorted lists.  Take from `list` on equality. */
static void *llist_merge(void *list, void *other,
			 void *(*get_next_fn)(const void *),
			 void (*set_next_fn)(void *, void *),
			 int (*compare_fn)(const void *, const void *))
{
	void *result = list, *tail;
	int prefer_list = compare_fn(list, other) <= 0;

	if (!prefer_list) {
		result = other;
		SWAP(list, other);
	}
	for (;;) {
		do {
			tail = list;
			list = get_next_fn(list);
			if (!list) {
				set_next_fn(tail, other);
				return result;
			}
		} while (compare_fn(list, other) < prefer_list);
		set_next_fn(tail, other);
		prefer_list ^= 1;
		SWAP(list, other);
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

	if (!list)
		return NULL;

	for (;;) {
		int i;
		size_t m;
		void *next = get_next_fn(list);
		if (next)
			set_next_fn(list, NULL);
		for (i = 0, m = n;; i++, m >>= 1) {
			if (m & 1)
				list = llist_merge(ranks[i], list, get_next_fn,
						   set_next_fn, compare_fn);
			else if (next)
				break;
			else if (!m)
				return list;
		}
		n++;
		ranks[i] = list;
		list = next;
	}
}
