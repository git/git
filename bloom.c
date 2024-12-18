#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "bloom.h"
#include "diff.h"
#include "diffcore.h"
#include "hashmap.h"
#include "commit-graph.h"
#include "commit.h"
#include "commit-slab.h"
#include "tree.h"
#include "tree-walk.h"
#include "config.h"
#include "repository.h"

define_commit_slab(bloom_filter_slab, struct bloom_filter);

static struct bloom_filter_slab bloom_filters;

struct pathmap_hash_entry {
    struct hashmap_entry entry;
    const char path[FLEX_ARRAY];
};

static uint32_t rotate_left(uint32_t value, int32_t count)
{
	uint32_t mask = 8 * sizeof(uint32_t) - 1;
	count &= mask;
	return ((value << count) | (value >> ((-count) & mask)));
}

static inline unsigned char get_bitmask(uint32_t pos)
{
	return ((unsigned char)1) << (pos & (BITS_PER_WORD - 1));
}

static int check_bloom_offset(struct commit_graph *g, uint32_t pos,
			      uint32_t offset)
{
	/*
	 * Note that we allow offsets equal to the data size, which would set
	 * our pointers at one past the end of the chunk memory. This is
	 * necessary because the on-disk index points to the end of the
	 * entries (so we can compute size by comparing adjacent ones). And
	 * naturally the final entry's end is one-past-the-end of the chunk.
	 */
	if (offset <= g->chunk_bloom_data_size - BLOOMDATA_CHUNK_HEADER_SIZE)
		return 0;

	warning("ignoring out-of-range offset (%"PRIuMAX") for changed-path"
		" filter at pos %"PRIuMAX" of %s (chunk size: %"PRIuMAX")",
		(uintmax_t)offset, (uintmax_t)pos,
		g->filename, (uintmax_t)g->chunk_bloom_data_size);
	return -1;
}

int load_bloom_filter_from_graph(struct commit_graph *g,
				 struct bloom_filter *filter,
				 uint32_t graph_pos)
{
	uint32_t lex_pos, start_index, end_index;

	while (graph_pos < g->num_commits_in_base)
		g = g->base_graph;

	/* The commit graph commit 'c' lives in doesn't carry Bloom filters. */
	if (!g->chunk_bloom_indexes)
		return 0;

	lex_pos = graph_pos - g->num_commits_in_base;

	end_index = get_be32(g->chunk_bloom_indexes + 4 * lex_pos);

	if (lex_pos > 0)
		start_index = get_be32(g->chunk_bloom_indexes + 4 * (lex_pos - 1));
	else
		start_index = 0;

	if (check_bloom_offset(g, lex_pos, end_index) < 0 ||
	    check_bloom_offset(g, lex_pos - 1, start_index) < 0)
		return 0;

	if (end_index < start_index) {
		warning("ignoring decreasing changed-path index offsets"
			" (%"PRIuMAX" > %"PRIuMAX") for positions"
			" %"PRIuMAX" and %"PRIuMAX" of %s",
			(uintmax_t)start_index, (uintmax_t)end_index,
			(uintmax_t)(lex_pos-1), (uintmax_t)lex_pos,
			g->filename);
		return 0;
	}

	filter->len = end_index - start_index;
	filter->data = (unsigned char *)(g->chunk_bloom_data +
					sizeof(unsigned char) * start_index +
					BLOOMDATA_CHUNK_HEADER_SIZE);
	filter->version = g->bloom_filter_settings->hash_version;
	filter->to_free = NULL;

	return 1;
}

/*
 * Calculate the murmur3 32-bit hash value for the given data
 * using the given seed.
 * Produces a uniformly distributed hash value.
 * Not considered to be cryptographically secure.
 * Implemented as described in https://en.wikipedia.org/wiki/MurmurHash#Algorithm
 */
uint32_t murmur3_seeded_v2(uint32_t seed, const char *data, size_t len)
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
		uint32_t byte1 = (uint32_t)(unsigned char)data[4*i];
		uint32_t byte2 = ((uint32_t)(unsigned char)data[4*i + 1]) << 8;
		uint32_t byte3 = ((uint32_t)(unsigned char)data[4*i + 2]) << 16;
		uint32_t byte4 = ((uint32_t)(unsigned char)data[4*i + 3]) << 24;
		k = byte1 | byte2 | byte3 | byte4;
		k *= c1;
		k = rotate_left(k, r1);
		k *= c2;

		seed ^= k;
		seed = rotate_left(seed, r2) * m + n;
	}

	tail = (data + len4 * sizeof(uint32_t));

	switch (len & (sizeof(uint32_t) - 1)) {
	case 3:
		k1 ^= ((uint32_t)(unsigned char)tail[2]) << 16;
		/*-fallthrough*/
	case 2:
		k1 ^= ((uint32_t)(unsigned char)tail[1]) << 8;
		/*-fallthrough*/
	case 1:
		k1 ^= ((uint32_t)(unsigned char)tail[0]) << 0;
		k1 *= c1;
		k1 = rotate_left(k1, r1);
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

static uint32_t murmur3_seeded_v1(uint32_t seed, const char *data, size_t len)
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
		k = rotate_left(k, r1);
		k *= c2;

		seed ^= k;
		seed = rotate_left(seed, r2) * m + n;
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
		k1 = rotate_left(k1, r1);
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

void fill_bloom_key(const char *data,
		    size_t len,
		    struct bloom_key *key,
		    const struct bloom_filter_settings *settings)
{
	int i;
	const uint32_t seed0 = 0x293ae76f;
	const uint32_t seed1 = 0x7e646e2c;
	uint32_t hash0, hash1;
	if (settings->hash_version == 2) {
		hash0 = murmur3_seeded_v2(seed0, data, len);
		hash1 = murmur3_seeded_v2(seed1, data, len);
	} else {
		hash0 = murmur3_seeded_v1(seed0, data, len);
		hash1 = murmur3_seeded_v1(seed1, data, len);
	}

	key->hashes = (uint32_t *)xcalloc(settings->num_hashes, sizeof(uint32_t));
	for (i = 0; i < settings->num_hashes; i++)
		key->hashes[i] = hash0 + i * hash1;
}

void clear_bloom_key(struct bloom_key *key)
{
	FREE_AND_NULL(key->hashes);
}

void add_key_to_filter(const struct bloom_key *key,
		       struct bloom_filter *filter,
		       const struct bloom_filter_settings *settings)
{
	int i;
	uint64_t mod = filter->len * BITS_PER_WORD;

	for (i = 0; i < settings->num_hashes; i++) {
		uint64_t hash_mod = key->hashes[i] % mod;
		uint64_t block_pos = hash_mod / BITS_PER_WORD;

		filter->data[block_pos] |= get_bitmask(hash_mod);
	}
}

void init_bloom_filters(void)
{
	init_bloom_filter_slab(&bloom_filters);
}

static void free_one_bloom_filter(struct bloom_filter *filter)
{
	if (!filter)
		return;
	free(filter->to_free);
}

void deinit_bloom_filters(void)
{
	deep_clear_bloom_filter_slab(&bloom_filters, free_one_bloom_filter);
}

static int pathmap_cmp(const void *hashmap_cmp_fn_data UNUSED,
		       const struct hashmap_entry *eptr,
		       const struct hashmap_entry *entry_or_key,
		       const void *keydata UNUSED)
{
	const struct pathmap_hash_entry *e1, *e2;

	e1 = container_of(eptr, const struct pathmap_hash_entry, entry);
	e2 = container_of(entry_or_key, const struct pathmap_hash_entry, entry);

	return strcmp(e1->path, e2->path);
}

static void init_truncated_large_filter(struct bloom_filter *filter,
					int version)
{
	filter->data = filter->to_free = xmalloc(1);
	filter->data[0] = 0xFF;
	filter->len = 1;
	filter->version = version;
}

#define VISITED   (1u<<21)
#define HIGH_BITS (1u<<22)

static int has_entries_with_high_bit(struct repository *r, struct tree *t)
{
	if (parse_tree(t))
		return 1;

	if (!(t->object.flags & VISITED)) {
		struct tree_desc desc;
		struct name_entry entry;

		init_tree_desc(&desc, &t->object.oid, t->buffer, t->size);
		while (tree_entry(&desc, &entry)) {
			size_t i;
			for (i = 0; i < entry.pathlen; i++) {
				if (entry.path[i] & 0x80) {
					t->object.flags |= HIGH_BITS;
					goto done;
				}
			}

			if (S_ISDIR(entry.mode)) {
				struct tree *sub = lookup_tree(r, &entry.oid);
				if (sub && has_entries_with_high_bit(r, sub)) {
					t->object.flags |= HIGH_BITS;
					goto done;
				}
			}

		}

done:
		t->object.flags |= VISITED;
	}

	return !!(t->object.flags & HIGH_BITS);
}

static int commit_tree_has_high_bit_paths(struct repository *r,
					  struct commit *c)
{
	struct tree *t;
	if (repo_parse_commit(r, c))
		return 1;
	t = repo_get_commit_tree(r, c);
	if (!t)
		return 1;
	return has_entries_with_high_bit(r, t);
}

static struct bloom_filter *upgrade_filter(struct repository *r, struct commit *c,
					   struct bloom_filter *filter,
					   int hash_version)
{
	struct commit_list *p = c->parents;
	if (commit_tree_has_high_bit_paths(r, c))
		return NULL;

	if (p && commit_tree_has_high_bit_paths(r, p->item))
		return NULL;

	filter->version = hash_version;

	return filter;
}

struct bloom_filter *get_bloom_filter(struct repository *r, struct commit *c)
{
	struct bloom_filter *filter;
	int hash_version;

	filter = get_or_compute_bloom_filter(r, c, 0, NULL, NULL);
	if (!filter)
		return NULL;

	prepare_repo_settings(r);
	hash_version = r->settings.commit_graph_changed_paths_version;

	if (!(hash_version == -1 || hash_version == filter->version))
		return NULL; /* unusable filter */
	return filter;
}

struct bloom_filter *get_or_compute_bloom_filter(struct repository *r,
						 struct commit *c,
						 int compute_if_not_present,
						 const struct bloom_filter_settings *settings,
						 enum bloom_filter_computed *computed)
{
	struct bloom_filter *filter;
	int i;
	struct diff_options diffopt;

	if (computed)
		*computed = BLOOM_NOT_COMPUTED;

	if (!bloom_filters.slab_size)
		return NULL;

	filter = bloom_filter_slab_at(&bloom_filters, c);

	if (!filter->data) {
		uint32_t graph_pos;
		if (repo_find_commit_pos_in_graph(r, c, &graph_pos))
			load_bloom_filter_from_graph(r->objects->commit_graph,
						     filter, graph_pos);
	}

	if (filter->data && filter->len) {
		struct bloom_filter *upgrade;
		if (!settings || settings->hash_version == filter->version)
			return filter;

		/* version mismatch, see if we can upgrade */
		if (compute_if_not_present &&
		    git_env_bool("GIT_TEST_UPGRADE_BLOOM_FILTERS", 1)) {
			upgrade = upgrade_filter(r, c, filter,
						 settings->hash_version);
			if (upgrade) {
				if (computed)
					*computed |= BLOOM_UPGRADED;
				return upgrade;
			}
		}
	}
	if (!compute_if_not_present)
		return NULL;

	repo_diff_setup(r, &diffopt);
	diffopt.flags.recursive = 1;
	diffopt.detect_rename = 0;
	diffopt.max_changes = settings->max_changed_paths;
	diff_setup_done(&diffopt);

	/* ensure commit is parsed so we have parent information */
	repo_parse_commit(r, c);

	if (c->parents)
		diff_tree_oid(&c->parents->item->object.oid, &c->object.oid, "", &diffopt);
	else
		diff_tree_oid(NULL, &c->object.oid, "", &diffopt);
	diffcore_std(&diffopt);

	if (diff_queued_diff.nr <= settings->max_changed_paths) {
		struct hashmap pathmap = HASHMAP_INIT(pathmap_cmp, NULL);
		struct pathmap_hash_entry *e;
		struct hashmap_iter iter;

		for (i = 0; i < diff_queued_diff.nr; i++) {
			const char *path = diff_queued_diff.queue[i]->two->path;

			/*
			 * Add each leading directory of the changed file, i.e. for
			 * 'dir/subdir/file' add 'dir' and 'dir/subdir' as well, so
			 * the Bloom filter could be used to speed up commands like
			 * 'git log dir/subdir', too.
			 *
			 * Note that directories are added without the trailing '/'.
			 */
			do {
				char *last_slash = strrchr(path, '/');

				FLEX_ALLOC_STR(e, path, path);
				hashmap_entry_init(&e->entry, strhash(path));

				if (!hashmap_get(&pathmap, &e->entry, NULL))
					hashmap_add(&pathmap, &e->entry);
				else
					free(e);

				if (!last_slash)
					last_slash = (char*)path;
				*last_slash = '\0';

			} while (*path);
		}

		if (hashmap_get_size(&pathmap) > settings->max_changed_paths) {
			init_truncated_large_filter(filter,
						    settings->hash_version);
			if (computed)
				*computed |= BLOOM_TRUNC_LARGE;
			goto cleanup;
		}

		filter->len = (hashmap_get_size(&pathmap) * settings->bits_per_entry + BITS_PER_WORD - 1) / BITS_PER_WORD;
		filter->version = settings->hash_version;
		if (!filter->len) {
			if (computed)
				*computed |= BLOOM_TRUNC_EMPTY;
			filter->len = 1;
		}
		CALLOC_ARRAY(filter->data, filter->len);
		filter->to_free = filter->data;

		hashmap_for_each_entry(&pathmap, &iter, e, entry) {
			struct bloom_key key;
			fill_bloom_key(e->path, strlen(e->path), &key, settings);
			add_key_to_filter(&key, filter, settings);
			clear_bloom_key(&key);
		}

	cleanup:
		hashmap_clear_and_free(&pathmap, struct pathmap_hash_entry, entry);
	} else {
		init_truncated_large_filter(filter, settings->hash_version);

		if (computed)
			*computed |= BLOOM_TRUNC_LARGE;
	}

	if (computed)
		*computed |= BLOOM_COMPUTED;

	diff_queue_clear(&diff_queued_diff);
	return filter;
}

int bloom_filter_contains(const struct bloom_filter *filter,
			  const struct bloom_key *key,
			  const struct bloom_filter_settings *settings)
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
