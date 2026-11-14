/*
 * A wrapper around cbtree which stores oids
 * May be used to replace oid-array for prefix (abbreviation) matches
 */
#include "git-compat-util.h"
#include "oidtree.h"
#include "hash.h"

struct oidtree_node {
	struct cb_node base;
	struct object_id key;
	void *data;
};

void oidtree_init(struct oidtree *ot)
{
	cb_init(&ot->tree, offsetof(struct oidtree_node, key));
	mem_pool_init(&ot->mem_pool, 0);
}

void oidtree_clear(struct oidtree *ot)
{
	if (ot) {
		mem_pool_discard(&ot->mem_pool, 0);
		oidtree_init(ot);
	}
}

struct oidtree_data {
	struct object_id oid;
};

void oidtree_insert(struct oidtree *ot, const struct object_id *oid,
		    void *data)
{
	struct oidtree_node *on;
	struct cb_node *node;

	if (!oid->algo)
		BUG("oidtree_insert requires oid->algo");

	on = mem_pool_alloc(&ot->mem_pool, sizeof(*on));
	oidcpy(&on->key, oid);
	on->data = data;

	/*
	 * n.b. Current callers won't get us duplicates, here.  If a
	 * future caller causes duplicates, there'll be a small leak
	 * that won't be freed until oidtree_clear.  Currently it's not
	 * worth maintaining a free list
	 */
	node = cb_insert(&ot->tree, &on->base, sizeof(*oid));
	if (node) {
		struct oidtree_node *preexisting = container_of(node, struct oidtree_node, base);
		preexisting->data = data;
	}
}

static struct oidtree_node *oidtree_lookup(struct oidtree *ot,
					   const struct object_id *oid)
{
	struct object_id k;
	size_t klen = sizeof(k);
	struct cb_node *node;

	oidcpy(&k, oid);

	if (oid->algo == GIT_HASH_UNKNOWN)
		klen -= sizeof(oid->algo);

	/* cb_lookup relies on memcmp on the struct, so order matters: */
	klen += BUILD_ASSERT_OR_ZERO(offsetof(struct object_id, hash) <
				offsetof(struct object_id, algo));

	node = cb_lookup(&ot->tree, (const uint8_t *)&k, klen);
	return node ? container_of(node, struct oidtree_node, base) : NULL;
}

bool oidtree_contains(struct oidtree *ot, const struct object_id *oid)
{
	struct oidtree_node *node = oidtree_lookup(ot, oid);
	return node ? 1 : 0;
}

void *oidtree_get(struct oidtree *ot, const struct object_id *oid)
{
	struct oidtree_node *node = oidtree_lookup(ot, oid);
	return node ? node->data : NULL;
}

struct oidtree_each_data {
	oidtree_each_cb cb;
	void *cb_data;
	size_t *last_nibble_at;
	uint32_t algo;
	uint8_t last_byte;
};

static int iter(struct cb_node *n, void *cb_data)
{
	struct oidtree_node *node = container_of(n, struct oidtree_node, base);
	struct oidtree_each_data *data = cb_data;

	if (data->algo != GIT_HASH_UNKNOWN && data->algo != node->key.algo)
		return 0;

	if (data->last_nibble_at) {
		if ((node->key.hash[*data->last_nibble_at] ^ data->last_byte) & 0xf0)
			return 0;
	}

	return data->cb(&node->key, node->data, data->cb_data);
}

int oidtree_each(struct oidtree *ot, const struct object_id *prefix,
		 size_t prefix_hex_len, oidtree_each_cb cb, void *cb_data)
{
	struct oidtree_each_data data = {
		.cb = cb,
		.cb_data = cb_data,
		.algo = prefix->algo,
	};
	size_t klen = prefix_hex_len / 2;
	assert(prefix_hex_len <= GIT_MAX_HEXSZ);

	if (prefix_hex_len & 1) {
		data.last_byte = prefix->hash[klen];
		data.last_nibble_at = &klen;
	}

	return cb_each(&ot->tree, prefix->hash, klen, iter, &data);
}
