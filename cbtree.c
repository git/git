/*
 * crit-bit tree implementation, does no allocations internally
 * For more information on crit-bit trees: https://cr.yp.to/critbit.html
 * Based on Adam Langley's adaptation of Dan Bernstein's public domain code
 * git clone https://github.com/agl/critbit.git
 */
#include "git-compat-util.h"
#include "cbtree.h"

static struct cb_node *cb_node_of(const void *p)
{
	return (struct cb_node *)((uintptr_t)p - 1);
}

/* locate the best match, does not do a final comparision */
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

	assert(!((uintptr_t)node & 1)); /* allocations must be aligned */

	if (!t->root) {		/* insert into empty tree */
		t->root = node;
		return NULL;	/* success */
	}

	/* see if a node already exists */
	p = cb_internal_best_match(t->root, node->k, klen);

	/* find first differing byte */
	for (newbyte = 0; newbyte < klen; newbyte++) {
		if (p->k[newbyte] != node->k[newbyte])
			goto different_byte_found;
	}
	return p;	/* element exists, let user deal with it */

different_byte_found:
	newotherbits = p->k[newbyte] ^ node->k[newbyte];
	newotherbits |= newotherbits >> 1;
	newotherbits |= newotherbits >> 2;
	newotherbits |= newotherbits >> 4;
	newotherbits = (newotherbits & ~(newotherbits >> 1)) ^ 255;
	c = p->k[newbyte];
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
		c = q->byte < klen ? node->k[q->byte] : 0;
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

	return p && !memcmp(p->k, k, klen) ? p : NULL;
}

static enum cb_next cb_descend(struct cb_node *p, cb_iter fn, void *arg)
{
	if (1 & (uintptr_t)p) {
		struct cb_node *q = cb_node_of(p);
		enum cb_next n = cb_descend(q->child[0], fn, arg);

		return n == CB_BREAK ? n : cb_descend(q->child[1], fn, arg);
	} else {
		return fn(p, arg);
	}
}

void cb_each(struct cb_tree *t, const uint8_t *kpfx, size_t klen,
			cb_iter fn, void *arg)
{
	struct cb_node *p = t->root;
	struct cb_node *top = p;
	size_t i = 0;

	if (!p) return; /* empty tree */

	/* Walk tree, maintaining top pointer */
	while (1 & (uintptr_t)p) {
		struct cb_node *q = cb_node_of(p);
		uint8_t c = q->byte < klen ? kpfx[q->byte] : 0;
		size_t direction = (1 + (q->otherbits | c)) >> 8;

		p = q->child[direction];
		if (q->byte < klen)
			top = p;
	}

	for (i = 0; i < klen; i++) {
		if (p->k[i] != kpfx[i])
			return; /* "best" match failed */
	}
	cb_descend(top, fn, arg);
}
