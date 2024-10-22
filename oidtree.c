/*
 * A wrapper around cbtree which stores oids
 * May be used to replace oid-array for prefix (abbreviation) matches
 */
#include "git-compat-util.h"
#include "oidtree.h"
#include "hash.h"

struct oidtree_iter_data {
	oidtree_iter fn;
	void *arg;
	size_t *last_nibble_at;
	int algo;
	uint8_t last_byte;
};

void oidtree_init(struct oidtree *ot)
{
	cb_init(&ot->tree);
	mem_pool_init(&ot->mem_pool, 0);
}

void oidtree_clear(struct oidtree *ot)
{
	if (ot) {
		mem_pool_discard(&ot->mem_pool, 0);
		oidtree_init(ot);
	}
}

void oidtree_insert(struct oidtree *ot, const struct object_id *oid)
{
	struct cb_node *on;
	struct object_id k;

	if (!oid->algo)
		BUG("oidtree_insert requires oid->algo");

	on = mem_pool_alloc(&ot->mem_pool, sizeof(*on) + sizeof(*oid));

	/*
	 * Clear the padding and copy the result in separate steps to
	 * respect the 4-byte alignment needed by struct object_id.
	 */
	oidcpy(&k, oid);
	memcpy(on->k, &k, sizeof(k));

	/*
	 * n.b. Current callers won't get us duplicates, here.  If a
	 * future caller causes duplicates, there'll be a small leak
	 * that won't be freed until oidtree_clear.  Currently it's not
	 * worth maintaining a free list
	 */
	cb_insert(&ot->tree, on, sizeof(*oid));
}


int oidtree_contains(struct oidtree *ot, const struct object_id *oid)
{
	struct object_id k;
	size_t klen = sizeof(k);

	oidcpy(&k, oid);

	if (oid->algo == GIT_HASH_UNKNOWN)
		klen -= sizeof(oid->algo);

	/* cb_lookup relies on memcmp on the struct, so order matters: */
	klen += BUILD_ASSERT_OR_ZERO(offsetof(struct object_id, hash) <
				offsetof(struct object_id, algo));

	return cb_lookup(&ot->tree, (const uint8_t *)&k, klen) ? 1 : 0;
}

static enum cb_next iter(struct cb_node *n, void *arg)
{
	struct oidtree_iter_data *x = arg;
	struct object_id k;

	/* Copy to provide 4-byte alignment needed by struct object_id. */
	memcpy(&k, n->k, sizeof(k));

	if (x->algo != GIT_HASH_UNKNOWN && x->algo != k.algo)
		return CB_CONTINUE;

	if (x->last_nibble_at) {
		if ((k.hash[*x->last_nibble_at] ^ x->last_byte) & 0xf0)
			return CB_CONTINUE;
	}

	return x->fn(&k, x->arg);
}

void oidtree_each(struct oidtree *ot, const struct object_id *oid,
			size_t oidhexsz, oidtree_iter fn, void *arg)
{
	size_t klen = oidhexsz / 2;
	struct oidtree_iter_data x = { 0 };
	assert(oidhexsz <= GIT_MAX_HEXSZ);

	x.fn = fn;
	x.arg = arg;
	x.algo = oid->algo;
	if (oidhexsz & 1) {
		x.last_byte = oid->hash[klen];
		x.last_nibble_at = &klen;
	}
	cb_each(&ot->tree, (const uint8_t *)oid, klen, iter, &x);
}
