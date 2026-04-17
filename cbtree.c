/*
 * crit-bit tree implementation, does no allocations internally
 * For more information on crit-bit trees: https://cr.yp.to/critbit.html
 * Based on Adam Langley's adaptation of Dan Bernstein's public domain code
 * git clone https://github.com/agl/critbit.git
 */
#include "git-compat-util.h"
#include "cbtree.h"

static inline uint8_t *cb_node_key(struct cb_tree *t, struct cb_node *node)
{
	return (uint8_t *) node + t->key_offset;
}

static struct cb_node *cb_node_of(const void *p)
{
	return (struct cb_node *)((uintptr_t)p - 1);
}

/* locate the best match, does not do a final comparison */
static struct cb_node *cb_internal_best_match(struct cb_node *p,
					const uint8_t *k, size_t klen)
{
	while (1 & (uintptr_t)p) {
		struct cb_node *q = cb_node_of(p);
		uint8_t c = q->byte < klen ? k[q->byte] : 0;
		size_t direction = (1 + (q->otherbits | c)) >> 8;

		p = q->child[direction];
	}
	return p;
}

/* returns NULL if successful, existing cb_node if duplicate */
struct cb_node *cb_insert(struct cb_tree *t, struct cb_node *node, size_t klen)
{
	size_t newbyte, newotherbits;
	uint8_t c;
	int newdirection;
	struct cb_node **wherep, *p;
	uint8_t *node_key, *p_key;

	assert(!((uintptr_t)node & 1)); /* allocations must be aligned */

	if (!t->root) {		/* insert into empty tree */
		t->root = node;
		return NULL;	/* success */
	}

	node_key = cb_node_key(t, node);

	/* see if a node already exists */
	p = cb_internal_best_match(t->root, node_key, klen);
	p_key = cb_node_key(t, p);

	/* find first differing byte */
	for (newbyte = 0; newbyte < klen; newbyte++) {
		if (p_key[newbyte] != node_key[newbyte])
			goto different_byte_found;
	}
	return p;	/* element exists, let user deal with it */

different_byte_found:
	newotherbits = p_key[newbyte] ^ node_key[newbyte];
	newotherbits |= newotherbits >> 1;
	newotherbits |= newotherbits >> 2;
	newotherbits |= newotherbits >> 4;
	newotherbits = (newotherbits & ~(newotherbits >> 1)) ^ 255;
	c = p_key[newbyte];
	newdirection = (1 + (newotherbits | c)) >> 8;

	node->byte = newbyte;
	node->otherbits = newotherbits;
	node->child[1 - newdirection] = node;

	/* find a place to insert it */
	wherep = &t->root;
	for (;;) {
		struct cb_node *q;
		size_t direction;

		p = *wherep;
		if (!(1 & (uintptr_t)p))
			break;
		q = cb_node_of(p);
		if (q->byte > newbyte)
			break;
		if (q->byte == newbyte && q->otherbits > newotherbits)
			break;
		c = q->byte < klen ? node_key[q->byte] : 0;
		direction = (1 + (q->otherbits | c)) >> 8;
		wherep = q->child + direction;
	}

	node->child[newdirection] = *wherep;
	*wherep = (struct cb_node *)(1 + (uintptr_t)node);

	return NULL; /* success */
}

struct cb_node *cb_lookup(struct cb_tree *t, const uint8_t *k, size_t klen)
{
	struct cb_node *p = cb_internal_best_match(t->root, k, klen);

	return p && !memcmp(cb_node_key(t, p), k, klen) ? p : NULL;
}

static int cb_descend(struct cb_node *p, cb_iter fn, void *arg)
{
	if (1 & (uintptr_t)p) {
		struct cb_node *q = cb_node_of(p);
		int ret = cb_descend(q->child[0], fn, arg);
		if (ret)
			return ret;
		return cb_descend(q->child[1], fn, arg);
	} else {
		return fn(p, arg);
	}
}

int cb_each(struct cb_tree *t, const uint8_t *kpfx, size_t klen,
	    cb_iter fn, void *arg)
{
	struct cb_node *p = t->root;
	struct cb_node *top = p;
	size_t i = 0;
	uint8_t *p_key;

	if (!p)
		return 0; /* empty tree */

	/* Walk tree, maintaining top pointer */
	while (1 & (uintptr_t)p) {
		struct cb_node *q = cb_node_of(p);
		uint8_t c = q->byte < klen ? kpfx[q->byte] : 0;
		size_t direction = (1 + (q->otherbits | c)) >> 8;

		p = q->child[direction];
		if (q->byte < klen)
			top = p;
	}

	p_key = cb_node_key(t, p);
	for (i = 0; i < klen; i++) {
		if (p_key[i] != kpfx[i])
			return 0; /* "best" match failed */
	}

	return cb_descend(top, fn, arg);
}
