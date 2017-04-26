#ifndef HASHMAP_H
#define HASHMAP_H

/*
 * Generic implementation of hash-based key-value mappings.
 * See Documentation/technical/api-hashmap.txt.
 */

/* FNV-1 functions */

extern unsigned int strhash(const char *buf);
extern unsigned int strihash(const char *buf);
extern unsigned int memhash(const void *buf, size_t len);
extern unsigned int memihash(const void *buf, size_t len);
extern unsigned int memihash_cont(unsigned int hash_seed, const void *buf, size_t len);

static inline unsigned int sha1hash(const unsigned char *sha1)
{
	/*
	 * Equivalent to 'return *(unsigned int *)sha1;', but safe on
	 * platforms that don't support unaligned reads.
	 */
	unsigned int hash;
	memcpy(&hash, sha1, sizeof(hash));
	return hash;
}

/* data structures */

struct hashmap_entry {
	struct hashmap_entry *next;
	unsigned int hash;
};

typedef int (*hashmap_cmp_fn)(const void *entry, const void *entry_or_key,
		const void *keydata);

struct hashmap {
	struct hashmap_entry **table;
	hashmap_cmp_fn cmpfn;
	unsigned int size, tablesize, grow_at, shrink_at;
	unsigned disallow_rehash : 1;
};

struct hashmap_iter {
	struct hashmap *map;
	struct hashmap_entry *next;
	unsigned int tablepos;
};

/* hashmap functions */

extern void hashmap_init(struct hashmap *map, hashmap_cmp_fn equals_function,
		size_t initial_size);
extern void hashmap_free(struct hashmap *map, int free_entries);

/* hashmap_entry functions */

static inline void hashmap_entry_init(void *entry, unsigned int hash)
{
	struct hashmap_entry *e = entry;
	e->hash = hash;
	e->next = NULL;
}
extern void *hashmap_get(const struct hashmap *map, const void *key,
		const void *keydata);
extern void *hashmap_get_next(const struct hashmap *map, const void *entry);
extern void hashmap_add(struct hashmap *map, void *entry);
extern void *hashmap_put(struct hashmap *map, void *entry);
extern void *hashmap_remove(struct hashmap *map, const void *key,
		const void *keydata);

static inline void *hashmap_get_from_hash(const struct hashmap *map,
		unsigned int hash, const void *keydata)
{
	struct hashmap_entry key;
	hashmap_entry_init(&key, hash);
	return hashmap_get(map, &key, keydata);
}

int hashmap_bucket(const struct hashmap *map, unsigned int hash);

/*
 * Disallow/allow rehashing of the hashmap.
 * This is useful if the caller knows that the hashmap
 * needs multi-threaded access.  The caller is still
 * required to guard/lock searches and inserts in a
 * manner appropriate to their usage.  This simply
 * prevents the table from being unexpectedly re-mapped.
 *
 * If is up to the caller to ensure that the hashmap is
 * initialized to a reasonable size to prevent poor
 * performance.
 *
 * When value=1, prevent future rehashes on adds and deleted.
 * When value=0, allow future rehahses.  This DOES NOT force
 * a rehash now.
 */
static inline void hashmap_disallow_rehash(struct hashmap *map, unsigned value)
{
	map->disallow_rehash = value;
}

/* hashmap_iter functions */

extern void hashmap_iter_init(struct hashmap *map, struct hashmap_iter *iter);
extern void *hashmap_iter_next(struct hashmap_iter *iter);
static inline void *hashmap_iter_first(struct hashmap *map,
		struct hashmap_iter *iter)
{
	hashmap_iter_init(map, iter);
	return hashmap_iter_next(iter);
}

/* string interning */

extern const void *memintern(const void *data, size_t len);
static inline const char *strintern(const char *string)
{
	return memintern(string, strlen(string));
}

#endif
