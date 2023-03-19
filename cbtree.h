/*
 * crit-bit tree implementation, does no allocations internally
 * For more information on crit-bit trees: https://cr.yp.to/critbit.html
 * Based on Adam Langley's adaptation of Dan Bernstein's public domain code
 * git clone https://github.com/agl/critbit.git
 *
 * This is adapted to store arbitrary data (not just NUL-terminated C strings
 * and allocates no memory internally.  The user needs to allocate
 * "struct cb_node" and fill cb_node.k[] with arbitrary match data
 * for memcmp.
 * If "klen" is variable, then it should be embedded into "c_node.k[]"
 * Recursion is bound by the maximum value of "klen" used.
 */
#ifndef CBTREE_H
#define CBTREE_H

struct cb_node;
struct cb_node {
	struct cb_node *child[2];
	/*
	 * n.b. uint32_t for `byte' is excessive for OIDs,
	 * we may consider shorter variants if nothing else gets stored.
	 */
	uint32_t byte;
	uint8_t otherbits;
	uint8_t k[FLEX_ARRAY]; /* arbitrary data, unaligned */
};

struct cb_tree {
	struct cb_node *root;
};

enum cb_next {
	CB_CONTINUE = 0,
	CB_BREAK = 1
};

#define CBTREE_INIT { 0 }

static inline void cb_init(struct cb_tree *t)
{
	struct cb_tree blank = CBTREE_INIT;
	memcpy(t, &blank, sizeof(*t));
}

struct cb_node *cb_lookup(struct cb_tree *, const uint8_t *k, size_t klen);
struct cb_node *cb_insert(struct cb_tree *, struct cb_node *, size_t klen);

typedef enum cb_next (*cb_iter)(struct cb_node *, void *arg);

void cb_each(struct cb_tree *, const uint8_t *kpfx, size_t klen,
		cb_iter, void *arg);

#endif /* CBTREE_H */
