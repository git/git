/*
 * C macro implementation of treaps.
 *
 * Usage:
 *   #include <stdint.h>
 *   #include "trp.h"
 *   trp_gen(...)
 *
 * Licensed under a two-clause BSD-style license.
 * See LICENSE for details.
 */

#ifndef TRP_H_
#define TRP_H_

#define MAYBE_UNUSED __attribute__((__unused__))

/* Node structure. */
struct trp_node {
	uint32_t trpn_left;
	uint32_t trpn_right;
};

/* Root structure. */
struct trp_root {
	uint32_t trp_root;
};

/* Pointer/Offset conversion. */
#define trpn_pointer(a_base, a_offset) (a_base##_pointer(a_offset))
#define trpn_offset(a_base, a_pointer) (a_base##_offset(a_pointer))
#define trpn_modify(a_base, a_offset) \
	do { \
		if ((a_offset) < a_base##_pool.committed) { \
			uint32_t old_offset = (a_offset);\
			(a_offset) = a_base##_alloc(1); \
			*trpn_pointer(a_base, a_offset) = \
				*trpn_pointer(a_base, old_offset); \
		} \
	} while (0)

/* Left accessors. */
#define trp_left_get(a_base, a_field, a_node) \
	(trpn_pointer(a_base, a_node)->a_field.trpn_left)
#define trp_left_set(a_base, a_field, a_node, a_left) \
	do { \
		trpn_modify(a_base, a_node); \
		trp_left_get(a_base, a_field, a_node) = (a_left); \
	} while (0)

/* Right accessors. */
#define trp_right_get(a_base, a_field, a_node) \
	(trpn_pointer(a_base, a_node)->a_field.trpn_right)
#define trp_right_set(a_base, a_field, a_node, a_right) \
	do { \
		trpn_modify(a_base, a_node); \
		trp_right_get(a_base, a_field, a_node) = (a_right); \
	} while (0)

/*
 * Fibonacci hash function.
 * The multiplier is the nearest prime to (2^32 times (√5 - 1)/2).
 * See Knuth §6.4: volume 3, 3rd ed, p518.
 */
#define trpn_hash(a_node) (uint32_t) (2654435761u * (a_node))

/* Priority accessors. */
#define trp_prio_get(a_node) trpn_hash(a_node)

/* Node initializer. */
#define trp_node_new(a_base, a_field, a_node) \
	do { \
		trp_left_set(a_base, a_field, (a_node), ~0); \
		trp_right_set(a_base, a_field, (a_node), ~0); \
	} while (0)

/* Internal utility macros. */
#define trpn_first(a_base, a_field, a_root, r_node) \
	do { \
		(r_node) = (a_root); \
		if ((r_node) == ~0) \
			return NULL; \
		while (~trp_left_get(a_base, a_field, (r_node))) \
			(r_node) = trp_left_get(a_base, a_field, (r_node)); \
	} while (0)

#define trpn_rotate_left(a_base, a_field, a_node, r_node) \
	do { \
		(r_node) = trp_right_get(a_base, a_field, (a_node)); \
		trp_right_set(a_base, a_field, (a_node), \
			trp_left_get(a_base, a_field, (r_node))); \
		trp_left_set(a_base, a_field, (r_node), (a_node)); \
	} while (0)

#define trpn_rotate_right(a_base, a_field, a_node, r_node) \
	do { \
		(r_node) = trp_left_get(a_base, a_field, (a_node)); \
		trp_left_set(a_base, a_field, (a_node), \
			trp_right_get(a_base, a_field, (r_node))); \
		trp_right_set(a_base, a_field, (r_node), (a_node)); \
	} while (0)

#define trp_gen(a_attr, a_pre, a_type, a_field, a_base, a_cmp) \
a_attr a_type MAYBE_UNUSED *a_pre##first(struct trp_root *treap) \
{ \
	uint32_t ret; \
	trpn_first(a_base, a_field, treap->trp_root, ret); \
	return trpn_pointer(a_base, ret); \
} \
a_attr a_type MAYBE_UNUSED *a_pre##next(struct trp_root *treap, a_type *node) \
{ \
	uint32_t ret; \
	uint32_t offset = trpn_offset(a_base, node); \
	if (~trp_right_get(a_base, a_field, offset)) { \
		trpn_first(a_base, a_field, \
			trp_right_get(a_base, a_field, offset), ret); \
	} else { \
		uint32_t tnode = treap->trp_root; \
		ret = ~0; \
		while (1) { \
			int cmp = (a_cmp)(trpn_pointer(a_base, offset), \
				trpn_pointer(a_base, tnode)); \
			if (cmp < 0) { \
				ret = tnode; \
				tnode = trp_left_get(a_base, a_field, tnode); \
			} else if (cmp > 0) { \
				tnode = trp_right_get(a_base, a_field, tnode); \
			} else { \
				break; \
			} \
		} \
	} \
	return trpn_pointer(a_base, ret); \
} \
a_attr a_type MAYBE_UNUSED *a_pre##search(struct trp_root *treap, a_type *key) \
{ \
	int cmp; \
	uint32_t ret = treap->trp_root; \
	while (~ret && (cmp = (a_cmp)(key, trpn_pointer(a_base, ret)))) { \
		if (cmp < 0) { \
			ret = trp_left_get(a_base, a_field, ret); \
		} else { \
			ret = trp_right_get(a_base, a_field, ret); \
		} \
	} \
	return trpn_pointer(a_base, ret); \
} \
a_attr a_type MAYBE_UNUSED *a_pre##nsearch(struct trp_root *treap, a_type *key) \
{ \
	int cmp; \
	uint32_t ret = treap->trp_root; \
	while (~ret && (cmp = (a_cmp)(key, trpn_pointer(a_base, ret)))) { \
		if (cmp < 0) { \
			if (!~trp_left_get(a_base, a_field, ret)) \
				break; \
			ret = trp_left_get(a_base, a_field, ret); \
		} else { \
			ret = trp_right_get(a_base, a_field, ret); \
		} \
	} \
	return trpn_pointer(a_base, ret); \
} \
a_attr uint32_t MAYBE_UNUSED a_pre##insert_recurse(uint32_t cur_node, uint32_t ins_node) \
{ \
	if (cur_node == ~0) { \
		return ins_node; \
	} else { \
		uint32_t ret; \
		int cmp = (a_cmp)(trpn_pointer(a_base, ins_node), \
					trpn_pointer(a_base, cur_node)); \
		if (cmp < 0) { \
			uint32_t left = a_pre##insert_recurse( \
				trp_left_get(a_base, a_field, cur_node), ins_node); \
			trp_left_set(a_base, a_field, cur_node, left); \
			if (trp_prio_get(left) < trp_prio_get(cur_node)) \
				trpn_rotate_right(a_base, a_field, cur_node, ret); \
			else \
				ret = cur_node; \
		} else { \
			uint32_t right = a_pre##insert_recurse( \
				trp_right_get(a_base, a_field, cur_node), ins_node); \
			trp_right_set(a_base, a_field, cur_node, right); \
			if (trp_prio_get(right) < trp_prio_get(cur_node)) \
				trpn_rotate_left(a_base, a_field, cur_node, ret); \
			else \
				ret = cur_node; \
		} \
		return ret; \
	} \
} \
a_attr a_type *MAYBE_UNUSED a_pre##insert(struct trp_root *treap, a_type *node) \
{ \
	uint32_t offset = trpn_offset(a_base, node); \
	trp_node_new(a_base, a_field, offset); \
	treap->trp_root = a_pre##insert_recurse(treap->trp_root, offset); \
	return trpn_pointer(a_base, offset); \
} \
a_attr uint32_t MAYBE_UNUSED a_pre##remove_recurse(uint32_t cur_node, uint32_t rem_node) \
{ \
	int cmp = a_cmp(trpn_pointer(a_base, rem_node), \
			trpn_pointer(a_base, cur_node)); \
	if (cmp == 0) { \
		uint32_t ret; \
		uint32_t left = trp_left_get(a_base, a_field, cur_node); \
		uint32_t right = trp_right_get(a_base, a_field, cur_node); \
		if (left == ~0) { \
			if (right == ~0) \
				return ~0; \
		} else if (right == ~0 || trp_prio_get(left) < trp_prio_get(right)) { \
			trpn_rotate_right(a_base, a_field, cur_node, ret); \
			right = a_pre##remove_recurse(cur_node, rem_node); \
			trp_right_set(a_base, a_field, ret, right); \
			return ret; \
		} \
		trpn_rotate_left(a_base, a_field, cur_node, ret); \
		left = a_pre##remove_recurse(cur_node, rem_node); \
		trp_left_set(a_base, a_field, ret, left); \
		return ret; \
	} else if (cmp < 0) { \
		uint32_t left = a_pre##remove_recurse( \
			trp_left_get(a_base, a_field, cur_node), rem_node); \
		trp_left_set(a_base, a_field, cur_node, left); \
		return cur_node; \
	} else { \
		uint32_t right = a_pre##remove_recurse( \
			trp_right_get(a_base, a_field, cur_node), rem_node); \
		trp_right_set(a_base, a_field, cur_node, right); \
		return cur_node; \
	} \
} \
a_attr void MAYBE_UNUSED a_pre##remove(struct trp_root *treap, a_type *node) \
{ \
	treap->trp_root = a_pre##remove_recurse(treap->trp_root, \
		trpn_offset(a_base, node)); \
} \

#endif
