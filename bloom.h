#ifndef BLOOM_H
#define BLOOM_H

struct commit;
struct repository;
struct commit_graph;

struct bloom_filter_settings {
	/*
	 * The version of the hashing technique being used.
	 * The newest version is 2, which is
	 * the seeded murmur3 hashing technique implemented
	 * in bloom.c. Bloom filters of version 1 were created
	 * with prior versions of Git, which had a bug in the
	 * implementation of the hash function.
	 */
	uint32_t hash_version;

	/*
	 * The number of times a path is hashed, i.e. the
	 * number of bit positions that cumulatively
	 * determine whether a path is present in the
	 * Bloom filter.
	 */
	uint32_t num_hashes;

	/*
	 * The minimum number of bits per entry in the Bloom
	 * filter. If the filter contains 'n' entries, then
	 * filter size is the minimum number of 8-bit words
	 * that contain n*b bits.
	 */
	uint32_t bits_per_entry;

	/*
	 * The maximum number of changed paths per commit
	 * before declaring a Bloom filter to be too-large.
	 *
	 * Not written to the commit-graph file.
	 */
	uint32_t max_changed_paths;
};

#define DEFAULT_BLOOM_MAX_CHANGES 512
#define DEFAULT_BLOOM_FILTER_SETTINGS { 1, 7, 10, DEFAULT_BLOOM_MAX_CHANGES }
#define BITS_PER_WORD 8
#define BLOOMDATA_CHUNK_HEADER_SIZE 3 * sizeof(uint32_t)

/*
 * A bloom_filter struct represents a data segment to
 * use when testing hash values. The 'len' member
 * dictates how many entries are stored in
 * 'data'.
 */
struct bloom_filter {
	unsigned char *data;
	size_t len;
	int version;

	void *to_free;
};

/*
 * A bloom_key represents the k hash values for a
 * given string. These can be precomputed and
 * stored in a bloom_key for re-use when testing
 * against a bloom_filter. The number of hashes is
 * given by the Bloom filter settings and is the same
 * for all Bloom filters and keys interacting with
 * the loaded version of the commit graph file and
 * the Bloom data chunks.
 */
struct bloom_key {
	uint32_t *hashes;
};

/*
 * A bloom_keyvec is a vector of bloom_keys, which
 * can be used to store multiple keys for a single
 * pathspec item.
 */
struct bloom_keyvec {
	size_t count;
	struct bloom_key key[FLEX_ARRAY];
};

int load_bloom_filter_from_graph(struct commit_graph *g,
				 struct bloom_filter *filter,
				 uint32_t graph_pos);

void bloom_key_fill(struct bloom_key *key, const char *data, size_t len,
		    const struct bloom_filter_settings *settings);
void bloom_key_clear(struct bloom_key *key);

/*
 * bloom_keyvec_new - Allocate and populate a bloom_keyvec with keys for the
 * given path.
 *
 * This function splits the input path by '/' and generates a bloom key for each
 * prefix, in reverse order of specificity. For example, given the input
 * "a/b/c", it will generate bloom keys for:
 *   - "a/b/c"
 *   - "a/b"
 *   - "a"
 *
 * The resulting keys are stored in a newly allocated bloom_keyvec.
 */
struct bloom_keyvec *bloom_keyvec_new(const char *path, size_t len,
				      const struct bloom_filter_settings *settings);
void bloom_keyvec_free(struct bloom_keyvec *vec);

void add_key_to_filter(const struct bloom_key *key,
		       struct bloom_filter *filter,
		       const struct bloom_filter_settings *settings);

void init_bloom_filters(void);
void deinit_bloom_filters(void);

enum bloom_filter_computed {
	BLOOM_NOT_COMPUTED = (1 << 0),
	BLOOM_COMPUTED     = (1 << 1),
	BLOOM_TRUNC_LARGE  = (1 << 2),
	BLOOM_TRUNC_EMPTY  = (1 << 3),
	BLOOM_UPGRADED     = (1 << 4),
};

struct bloom_filter *get_or_compute_bloom_filter(struct repository *r,
						 struct commit *c,
						 int compute_if_not_present,
						 const struct bloom_filter_settings *settings,
						 enum bloom_filter_computed *computed);

/*
 * Find the Bloom filter associated with the given commit "c".
 *
 * If any of the following are true
 *
 *   - the repository does not have a commit-graph, or
 *   - the repository disables reading from the commit-graph, or
 *   - the given commit does not have a Bloom filter computed, or
 *   - there is a Bloom filter for commit "c", but it cannot be read
 *     because the filter uses an incompatible version of murmur3
 *
 * , then `get_bloom_filter()` will return NULL. Otherwise, the corresponding
 * Bloom filter will be returned.
 *
 * For callers who wish to inspect Bloom filters with incompatible hash
 * versions, use get_or_compute_bloom_filter().
 */
struct bloom_filter *get_bloom_filter(struct repository *r, struct commit *c);

int bloom_filter_contains(const struct bloom_filter *filter,
			  const struct bloom_key *key,
			  const struct bloom_filter_settings *settings);

/*
 * bloom_filter_contains_vec - Check if all keys in a key vector are in the
 * Bloom filter.
 *
 * Returns 1 if **all** keys in the vector are present in the filter,
 * 0 if **any** key is not present.
 */
int bloom_filter_contains_vec(const struct bloom_filter *filter,
			      const struct bloom_keyvec *v,
			      const struct bloom_filter_settings *settings);

uint32_t test_bloom_murmur3_seeded(uint32_t seed, const char *data, size_t len,
				   int version);

#endif
