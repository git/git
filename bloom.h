#ifndef BLOOM_H
#define BLOOM_H

struct commit;
struct repository;

struct bloom_filter_settings {
	/*
	 * The version of the hashing technique being used.
	 * We currently only support version = 1 which is
	 * the seeded murmur3 hashing technique implemented
	 * in bloom.c.
	 */
	uint32_t hash_version;

	/*
	 * The number of times a path is hashed, i.e. the
	 * number of bit positions tht cumulatively
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
 * Calculate the murmur3 32-bit hash value for the given data
 * using the given seed.
 * Produces a uniformly distributed hash value.
 * Not considered to be cryptographically secure.
 * Implemented as described in https://en.wikipedia.org/wiki/MurmurHash#Algorithm
 */
uint32_t murmur3_seeded(uint32_t seed, const char *data, size_t len);

void fill_bloom_key(const char *data,
		    size_t len,
		    struct bloom_key *key,
		    const struct bloom_filter_settings *settings);
void clear_bloom_key(struct bloom_key *key);

void add_key_to_filter(const struct bloom_key *key,
		       struct bloom_filter *filter,
		       const struct bloom_filter_settings *settings);

void init_bloom_filters(void);

struct bloom_filter *get_bloom_filter(struct repository *r,
				      struct commit *c,
				      int compute_if_not_present);

int bloom_filter_contains(const struct bloom_filter *filter,
			  const struct bloom_key *key,
			  const struct bloom_filter_settings *settings);

#endif
