/*
 * A wrapper around cbtree which stores oids
 * May be used to replace oid-array for prefix (abbreviation) matches
 */
#include "git-compat-util.h"
#include "oidtree.h"
#include "hash.h"

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

bool oidtree_contains(struct oidtree *ot, const struct object_id *oid)
{
	struct object_id k;
	size_t klen = sizeof(k);

	oidcpy(&k, oid);

	if (oid->algo == GIT_HASH_UNKNOWN)
		klen -= sizeof(oid->algo);

	/* cb_lookup relies on memcmp on the struct, so order matters: */
	klen += BUILD_ASSERT_OR_ZERO(offsetof(struct object_id, hash) <
				offsetof(struct object_id, algo));

	return !!cb_lookup(&ot->tree, (const uint8_t *)&k, klen);
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
	struct oidtree_each_data *data = cb_data;
	struct object_id k;

	/* Copy to provide 4-byte alignment needed by struct object_id. */
	memcpy(&k, n->k, sizeof(k));

	if (data->algo != GIT_HASH_UNKNOWN && data->algo != k.algo)
		return 0;

	if (data->last_nibble_at) {
		if ((k.hash[*data->last_nibble_at] ^ data->last_byte) & 0xf0)
			return 0;
	}

	return data->cb(&k, data->cb_data);
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
