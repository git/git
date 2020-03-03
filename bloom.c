#include "git-compat-util.h"
#include "bloom.h"
#include "commit.h"
#include "commit-slab.h"
#include "commit-graph.h"
#include "object-store.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"
#include "hashmap.h"

define_commit_slab(bloom_filter_slab, struct bloom_filter);

struct bloom_filter_slab bloom_filters;

struct pathmap_hash_entry {
    struct hashmap_entry entry;
    const char path[FLEX_ARRAY];
};

static uint32_t rotate_right(uint32_t value, int32_t count)
{
	uint32_t mask = 8 * sizeof(uint32_t) - 1;
	count &= mask;
	return ((value >> count) | (value << ((-count) & mask)));
}

/*
 * Calculate a hash value for the given data using the given seed.
 * Produces a uniformly distributed hash value.
 * Not considered to be cryptographically secure.
 * Implemented as described in https://en.wikipedia.org/wiki/MurmurHash#Algorithm
 **/
static uint32_t seed_murmur3(uint32_t seed, const char *data, int len)
{
	const uint32_t c1 = 0xcc9e2d51;
	const uint32_t c2 = 0x1b873593;
	const uint32_t r1 = 15;
	const uint32_t r2 = 13;
	const uint32_t m = 5;
	const uint32_t n = 0xe6546b64;
	int i;
	uint32_t k1 = 0;
	const char *tail;

	int len4 = len / sizeof(uint32_t);

	uint32_t k;
	for (i = 0; i < len4; i++) {
		uint32_t byte1 = (uint32_t)data[4*i];
		uint32_t byte2 = ((uint32_t)data[4*i + 1]) << 8;
		uint32_t byte3 = ((uint32_t)data[4*i + 2]) << 16;
		uint32_t byte4 = ((uint32_t)data[4*i + 3]) << 24;
		k = byte1 | byte2 | byte3 | byte4;
		k *= c1;
		k = rotate_right(k, r1);
		k *= c2;

		seed ^= k;
		seed = rotate_right(seed, r2) * m + n;
	}

	tail = (data + len4 * sizeof(uint32_t));

	switch (len & (sizeof(uint32_t) - 1)) {
	case 3:
		k1 ^= ((uint32_t)tail[2]) << 16;
		/*-fallthrough*/
	case 2:
		k1 ^= ((uint32_t)tail[1]) << 8;
		/*-fallthrough*/
	case 1:
		k1 ^= ((uint32_t)tail[0]) << 0;
		k1 *= c1;
		k1 = rotate_right(k1, r1);
		k1 *= c2;
		seed ^= k1;
		break;
	}

	seed ^= (uint32_t)len;
	seed ^= (seed >> 16);
	seed *= 0x85ebca6b;
	seed ^= (seed >> 13);
	seed *= 0xc2b2ae35;
	seed ^= (seed >> 16);

	return seed;
}

static inline unsigned char get_bitmask(uint32_t pos)
{
	return ((unsigned char)1) << (pos & (BITS_PER_WORD - 1));
}

void load_bloom_filters(void)
{
	init_bloom_filter_slab(&bloom_filters);
}

void fill_bloom_key(const char *data,
					int len,
					struct bloom_key *key,
					struct bloom_filter_settings *settings)
{
	int i;
	const uint32_t seed0 = 0x293ae76f;
	const uint32_t seed1 = 0x7e646e2c;
	const uint32_t hash0 = seed_murmur3(seed0, data, len);
	const uint32_t hash1 = seed_murmur3(seed1, data, len);

	key->hashes = (uint32_t *)xcalloc(settings->num_hashes, sizeof(uint32_t));
	for (i = 0; i < settings->num_hashes; i++)
		key->hashes[i] = hash0 + i * hash1;
}

void add_key_to_filter(struct bloom_key *key,
					   struct bloom_filter *filter,
					   struct bloom_filter_settings *settings)
{
	int i;
	uint64_t mod = filter->len * BITS_PER_WORD;

	for (i = 0; i < settings->num_hashes; i++) {
		uint64_t hash_mod = key->hashes[i] % mod;
		uint64_t block_pos = hash_mod / BITS_PER_WORD;

		filter->data[block_pos] |= get_bitmask(hash_mod);
	}
}

static int load_bloom_filter_from_graph(struct commit_graph *g,
				   struct bloom_filter *filter,
				   struct commit *c)
{
	uint32_t lex_pos, start_index, end_index;

	while (c->graph_pos < g->num_commits_in_base)
		g = g->base_graph;

	/* The commit graph commit 'c' lives in doesn't carry bloom filters. */
	if (!g->chunk_bloom_indexes)
		return 0;

	lex_pos = c->graph_pos - g->num_commits_in_base;

	end_index = get_be32(g->chunk_bloom_indexes + 4 * lex_pos);

	if (lex_pos)
		start_index = get_be32(g->chunk_bloom_indexes + 4 * (lex_pos - 1));
	else
		start_index = 0;

	filter->len = end_index - start_index;
	filter->data = (unsigned char *)(g->chunk_bloom_data +
					sizeof(unsigned char) * start_index +
					BLOOMDATA_CHUNK_HEADER_SIZE);

	return 1;
}

struct bloom_filter *get_bloom_filter(struct repository *r,
				      struct commit *c,
				      int compute_if_not_present)
{
	struct bloom_filter *filter;
	struct bloom_filter_settings settings = DEFAULT_BLOOM_FILTER_SETTINGS;
	int i;
	struct diff_options diffopt;
	int max_changes = 512;

	if (!bloom_filters.slab_size)
		return NULL;

	filter = bloom_filter_slab_at(&bloom_filters, c);

	if (!filter->data) {
		load_commit_graph_info(r, c);
		if (c->graph_pos != COMMIT_NOT_FROM_GRAPH &&
			r->objects->commit_graph->chunk_bloom_indexes) {
			if (load_bloom_filter_from_graph(r->objects->commit_graph, filter, c))
				return filter;
			else
				return NULL;
		}
	}

	if (filter->data || !compute_if_not_present)
		return filter;

	repo_diff_setup(r, &diffopt);
	diffopt.flags.recursive = 1;
	diffopt.max_changes = max_changes;
	diff_setup_done(&diffopt);

	if (c->parents)
		diff_tree_oid(&c->parents->item->object.oid, &c->object.oid, "", &diffopt);
	else
		diff_tree_oid(NULL, &c->object.oid, "", &diffopt);
	diffcore_std(&diffopt);

	if (diff_queued_diff.nr <= max_changes) {
		struct hashmap pathmap;
		struct pathmap_hash_entry* e;
		struct hashmap_iter iter;
		hashmap_init(&pathmap, NULL, NULL, 0);

		for (i = 0; i < diff_queued_diff.nr; i++) {
			const char* path = diff_queued_diff.queue[i]->two->path;
			const char* p = path;

			/*
			* Add each leading directory of the changed file, i.e. for
			* 'dir/subdir/file' add 'dir' and 'dir/subdir' as well, so
			* the Bloom filter could be used to speed up commands like
			* 'git log dir/subdir', too.
			*
			* Note that directories are added without the trailing '/'.
			*/
			do {
				char* last_slash = strrchr(p, '/');

				FLEX_ALLOC_STR(e, path, path);
				hashmap_entry_init(&e->entry, strhash(p));
				hashmap_add(&pathmap, &e->entry);

				if (!last_slash)
					last_slash = (char*)p;
				*last_slash = '\0';

			} while (*p);

			diff_free_filepair(diff_queued_diff.queue[i]);
		}

		filter->len = (hashmap_get_size(&pathmap) * settings.bits_per_entry + BITS_PER_WORD - 1) / BITS_PER_WORD;
		filter->data = xcalloc(filter->len, sizeof(unsigned char));

		hashmap_for_each_entry(&pathmap, &iter, e, entry) {
			struct bloom_key key;
			fill_bloom_key(e->path, strlen(e->path), &key, &settings);
			add_key_to_filter(&key, filter, &settings);
		}

		hashmap_free_entries(&pathmap, struct pathmap_hash_entry, entry);
	} else {
		for (i = 0; i < diff_queued_diff.nr; i++)
			diff_free_filepair(diff_queued_diff.queue[i]);
		filter->data = NULL;
		filter->len = 0;
	}

	free(diff_queued_diff.queue);
	DIFF_QUEUE_CLEAR(&diff_queued_diff);

	return filter;
}

int bloom_filter_contains(struct bloom_filter *filter,
			  struct bloom_key *key,
			  struct bloom_filter_settings *settings)
{
	int i;
	uint64_t mod = filter->len * BITS_PER_WORD;

	if (!mod)
		return -1;

	for (i = 0; i < settings->num_hashes; i++) {
		uint64_t hash_mod = key->hashes[i] % mod;
		uint64_t block_pos = hash_mod / BITS_PER_WORD;
		if (!(filter->data[block_pos] & get_bitmask(hash_mod)))
			return 0;
	}

	return 1;
}
