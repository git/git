#ifndef MERGESORT_H
#define MERGESORT_H

/* Combine two sorted lists.  Take from `list` on equality. */
#define DEFINE_LIST_MERGE_INTERNAL(name, type)				\
static type *name##__merge(type *list, type *other,			\
			   int (*compare_fn)(const type *, const type *))\
{									\
	type *result = list, *tail;					\
	int prefer_list = compare_fn(list, other) <= 0;			\
									\
	if (!prefer_list) {						\
		result = other;						\
		SWAP(list, other);					\
	}								\
	for (;;) {							\
		do {							\
			tail = list;					\
			list = name##__get_next(list);			\
			if (!list) {					\
				name##__set_next(tail, other);		\
				return result;				\
			}						\
		} while (compare_fn(list, other) < prefer_list);	\
		name##__set_next(tail, other);				\
		prefer_list ^= 1;					\
		SWAP(list, other);					\
	}								\
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
#define DEFINE_LIST_SORT_INTERNAL(scope, name, type)			\
scope void name(type **listp,						\
		int (*compare_fn)(const type *, const type *))		\
{									\
	type *list = *listp;						\
	type *ranks[bitsizeof(type *)];					\
	size_t n = 0;							\
									\
	if (!list)							\
		return;							\
									\
	for (;;) {							\
		int i;							\
		size_t m;						\
		type *next = name##__get_next(list);			\
		if (next)						\
			name##__set_next(list, NULL);			\
		for (i = 0, m = n;; i++, m >>= 1) {			\
			if (m & 1) {					\
				list = name##__merge(ranks[i], list,	\
						    compare_fn);	\
			} else if (next) {				\
				break;					\
			} else if (!m) {				\
				*listp = list;				\
				return;					\
			}						\
		}							\
		n++;							\
		ranks[i] = list;					\
		list = next;						\
	}								\
}

#define DECLARE_LIST_SORT(scope, name, type)			\
scope void name(type **listp,					\
		int (*compare_fn)(const type *, const type *))

#define DEFINE_LIST_SORT_DEBUG(scope, name, type, next_member,	\
			       on_get_next, on_set_next)	\
								\
static inline type *name##__get_next(const type *elem)		\
{								\
	on_get_next;						\
	return elem->next_member;				\
}								\
								\
static inline void name##__set_next(type *elem, type *next)	\
{								\
	on_set_next;						\
	elem->next_member = next;				\
}								\
								\
DEFINE_LIST_MERGE_INTERNAL(name, type)				\
DEFINE_LIST_SORT_INTERNAL(scope, name, type)			\
DECLARE_LIST_SORT(scope, name, type)

#define DEFINE_LIST_SORT(scope, name, type, next_member) \
DEFINE_LIST_SORT_DEBUG(scope, name, type, next_member, (void)0, (void)0)

#endif
