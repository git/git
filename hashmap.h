#ifndef HASHMAP_H
#define HASHMAP_H

/*
 * Generic implementation of hash-based key-value mappings.
 *
 * An example that maps long to a string:
 * For the sake of the example this allows to lookup exact values, too
 * (i.e. it is operated as a set, the value is part of the key)
 * -------------------------------------
 *
 * struct hashmap map;
 * struct long2string {
 *     struct hashmap_entry ent; // must be the first member!
 *     long key;
 *     char value[FLEX_ARRAY];   // be careful with allocating on stack!
 * };
 *
 * #define COMPARE_VALUE 1
 *
 * static int long2string_cmp(const void *hashmap_cmp_fn_data,
 *                            const struct long2string *e1,
 *                            const struct long2string *e2,
 *                            const void *keydata)
 * {
 *     const char *string = keydata;
 *     unsigned flags = *(unsigned *)hashmap_cmp_fn_data;
 *
 *     if (flags & COMPARE_VALUE)
 *         return e1->key != e2->key ||
 *                  strcmp(e1->value, string ? string : e2->value);
 *     else
 *         return e1->key != e2->key;
 * }
 *
 * int main(int argc, char **argv)
 * {
 *     long key;
 *     char value[255], action[32];
 *     unsigned flags = 0;
 *
 *     hashmap_init(&map, (hashmap_cmp_fn) long2string_cmp, &flags, 0);
 *
 *     while (scanf("%s %ld %s", action, &key, value)) {
 *
 *         if (!strcmp("add", action)) {
 *             struct long2string *e;
 *             FLEX_ALLOC_STR(e, value, value);
 *             hashmap_entry_init(e, memhash(&key, sizeof(long)));
 *             e->key = key;
 *             hashmap_add(&map, e);
 *         }
 *
 *         if (!strcmp("print_all_by_key", action)) {
 *             struct long2string k, *e;
 *             hashmap_entry_init(&k, memhash(&key, sizeof(long)));
 *             k.key = key;
 *
 *             flags &= ~COMPARE_VALUE;
 *             e = hashmap_get(&map, &k, NULL);
 *             if (e) {
 *                 printf("first: %ld %s\n", e->key, e->value);
 *                 while ((e = hashmap_get_next(&map, e)))
 *                     printf("found more: %ld %s\n", e->key, e->value);
 *             }
 *         }
 *
 *         if (!strcmp("has_exact_match", action)) {
 *             struct long2string *e;
 *             FLEX_ALLOC_STR(e, value, value);
 *             hashmap_entry_init(e, memhash(&key, sizeof(long)));
 *             e->key = key;
 *
 *             flags |= COMPARE_VALUE;
 *             printf("%sfound\n", hashmap_get(&map, e, NULL) ? "" : "not ");
 *             free(e);
 *         }
 *
 *         if (!strcmp("has_exact_match_no_heap_alloc", action)) {
 *             struct long2string k;
 *             hashmap_entry_init(&k, memhash(&key, sizeof(long)));
 *             k.key = key;
 *
 *             flags |= COMPARE_VALUE;
 *             printf("%sfound\n", hashmap_get(&map, &k, value) ? "" : "not ");
 *         }
 *
 *         if (!strcmp("end", action)) {
 *             hashmap_free(&map, 1);
 *             break;
 *         }
 *     }
 *
 *     return 0;
 * }
 */

/*
 * Ready-to-use hash functions for strings, using the FNV-1 algorithm (see
 * http://www.isthe.com/chongo/tech/comp/fnv).
 * `strhash` and `strihash` take 0-terminated strings, while `memhash` and
 * `memihash` operate on arbitrary-length memory.
 * `strihash` and `memihash` are case insensitive versions.
 * `memihash_cont` is a variant of `memihash` that allows a computation to be
 * continued with another chunk of data.
 */
extern unsigned int strhash(const char *buf);
extern unsigned int strihash(const char *buf);
extern unsigned int memhash(const void *buf, size_t len);
extern unsigned int memihash(const void *buf, size_t len);
extern unsigned int memihash_cont(unsigned int hash_seed, const void *buf, size_t len);

/*
 * Converts a cryptographic hash (e.g. SHA-1) into an int-sized hash code
 * for use in hash tables. Cryptographic hashes are supposed to have
 * uniform distribution, so in contrast to `memhash()`, this just copies
 * the first `sizeof(int)` bytes without shuffling any bits. Note that
 * the results will be different on big-endian and little-endian
 * platforms, so they should not be stored or transferred over the net.
 */
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

/*
 * struct hashmap_entry is an opaque structure representing an entry in the
 * hash table, which must be used as first member of user data structures.
 * Ideally it should be followed by an int-sized member to prevent unused
 * memory on 64-bit systems due to alignment.
 */
struct hashmap_entry {
	/*
	 * next points to the next entry in case of collisions (i.e. if
	 * multiple entries map to the same bucket)
	 */
	struct hashmap_entry *next;

	/* entry's hash code */
	unsigned int hash;
};

/*
 * User-supplied function to test two hashmap entries for equality. Shall
 * return 0 if the entries are equal.
 *
 * This function is always called with non-NULL `entry` and `entry_or_key`
 * parameters that have the same hash code.
 *
 * When looking up an entry, the `key` and `keydata` parameters to hashmap_get
 * and hashmap_remove are always passed as second `entry_or_key` and third
 * argument `keydata`, respectively. Otherwise, `keydata` is NULL.
 *
 * When it is too expensive to allocate a user entry (either because it is
 * large or varialbe sized, such that it is not on the stack), then the
 * relevant data to check for equality should be passed via `keydata`.
 * In this case `key` can be a stripped down version of the user key data
 * or even just a hashmap_entry having the correct hash.
 *
 * The `hashmap_cmp_fn_data` entry is the pointer given in the init function.
 */
typedef int (*hashmap_cmp_fn)(const void *hashmap_cmp_fn_data,
			      const void *entry, const void *entry_or_key,
			      const void *keydata);

/*
 * struct hashmap is the hash table structure. Members can be used as follows,
 * but should not be modified directly.
 */
struct hashmap {
	struct hashmap_entry **table;

	/* Stores the comparison function specified in `hashmap_init()`. */
	hashmap_cmp_fn cmpfn;
	const void *cmpfn_data;

	/* total number of entries (0 means the hashmap is empty) */
	unsigned int private_size; /* use hashmap_get_size() */

	/*
	 * tablesize is the allocated size of the hash table. A non-0 value
	 * indicates that the hashmap is initialized. It may also be useful
	 * for statistical purposes (i.e. `size / tablesize` is the current
	 * load factor).
	 */
	unsigned int tablesize;

	unsigned int grow_at;
	unsigned int shrink_at;

	unsigned int do_count_items : 1;
};

/* hashmap functions */

/*
 * Initializes a hashmap structure.
 *
 * `map` is the hashmap to initialize.
 *
 * The `equals_function` can be specified to compare two entries for equality.
 * If NULL, entries are considered equal if their hash codes are equal.
 *
 * The `equals_function_data` parameter can be used to provide additional data
 * (a callback cookie) that will be passed to `equals_function` each time it
 * is called. This allows a single `equals_function` to implement multiple
 * comparison functions.
 *
 * If the total number of entries is known in advance, the `initial_size`
 * parameter may be used to preallocate a sufficiently large table and thus
 * prevent expensive resizing. If 0, the table is dynamically resized.
 */
extern void hashmap_init(struct hashmap *map,
			 hashmap_cmp_fn equals_function,
			 const void *equals_function_data,
			 size_t initial_size);

/*
 * Frees a hashmap structure and allocated memory.
 *
 * If `free_entries` is true, each hashmap_entry in the map is freed as well
 * using stdlibs free().
 */
extern void hashmap_free(struct hashmap *map, int free_entries);

/* hashmap_entry functions */

/*
 * Initializes a hashmap_entry structure.
 *
 * `entry` points to the entry to initialize.
 * `hash` is the hash code of the entry.
 *
 * The hashmap_entry structure does not hold references to external resources,
 * and it is safe to just discard it once you are done with it (i.e. if
 * your structure was allocated with xmalloc(), you can just free(3) it,
 * and if it is on stack, you can just let it go out of scope).
 */
static inline void hashmap_entry_init(void *entry, unsigned int hash)
{
	struct hashmap_entry *e = entry;
	e->hash = hash;
	e->next = NULL;
}

/*
 * Return the number of items in the map.
 */
static inline unsigned int hashmap_get_size(struct hashmap *map)
{
	if (map->do_count_items)
		return map->private_size;

	BUG("hashmap_get_size: size not set");
	return 0;
}

/*
 * Returns the hashmap entry for the specified key, or NULL if not found.
 *
 * `map` is the hashmap structure.
 *
 * `key` is a user data structure that starts with hashmap_entry that has at
 * least been initialized with the proper hash code (via `hashmap_entry_init`).
 *
 * `keydata` is a data structure that holds just enough information to check
 * for equality to a given entry.
 *
 * If the key data is variable-sized (e.g. a FLEX_ARRAY string) or quite large,
 * it is undesirable to create a full-fledged entry structure on the heap and
 * copy all the key data into the structure.
 *
 * In this case, the `keydata` parameter can be used to pass
 * variable-sized key data directly to the comparison function, and the `key`
 * parameter can be a stripped-down, fixed size entry structure allocated on the
 * stack.
 *
 * If an entry with matching hash code is found, `key` and `keydata` are passed
 * to `hashmap_cmp_fn` to decide whether the entry matches the key.
 */
extern void *hashmap_get(const struct hashmap *map, const void *key,
			 const void *keydata);

/*
 * Returns the hashmap entry for the specified hash code and key data,
 * or NULL if not found.
 *
 * `map` is the hashmap structure.
 * `hash` is the hash code of the entry to look up.
 *
 * If an entry with matching hash code is found, `keydata` is passed to
 * `hashmap_cmp_fn` to decide whether the entry matches the key. The
 * `entry_or_key` parameter of `hashmap_cmp_fn` points to a hashmap_entry
 * structure that should not be used in the comparison.
 */
static inline void *hashmap_get_from_hash(const struct hashmap *map,
					  unsigned int hash,
					  const void *keydata)
{
	struct hashmap_entry key;
	hashmap_entry_init(&key, hash);
	return hashmap_get(map, &key, keydata);
}

/*
 * Returns the next equal hashmap entry, or NULL if not found. This can be
 * used to iterate over duplicate entries (see `hashmap_add`).
 *
 * `map` is the hashmap structure.
 * `entry` is the hashmap_entry to start the search from, obtained via a previous
 * call to `hashmap_get` or `hashmap_get_next`.
 */
extern void *hashmap_get_next(const struct hashmap *map, const void *entry);

/*
 * Adds a hashmap entry. This allows to add duplicate entries (i.e.
 * separate values with the same key according to hashmap_cmp_fn).
 *
 * `map` is the hashmap structure.
 * `entry` is the entry to add.
 */
extern void hashmap_add(struct hashmap *map, void *entry);

/*
 * Adds or replaces a hashmap entry. If the hashmap contains duplicate
 * entries equal to the specified entry, only one of them will be replaced.
 *
 * `map` is the hashmap structure.
 * `entry` is the entry to add or replace.
 * Returns the replaced entry, or NULL if not found (i.e. the entry was added).
 */
extern void *hashmap_put(struct hashmap *map, void *entry);

/*
 * Removes a hashmap entry matching the specified key. If the hashmap contains
 * duplicate entries equal to the specified key, only one of them will be
 * removed. Returns the removed entry, or NULL if not found.
 *
 * Argument explanation is the same as in `hashmap_get`.
 */
extern void *hashmap_remove(struct hashmap *map, const void *key,
		const void *keydata);

/*
 * Returns the `bucket` an entry is stored in.
 * Useful for multithreaded read access.
 */
int hashmap_bucket(const struct hashmap *map, unsigned int hash);

/*
 * Used to iterate over all entries of a hashmap. Note that it is
 * not safe to add or remove entries to the hashmap while
 * iterating.
 */
struct hashmap_iter {
	struct hashmap *map;
	struct hashmap_entry *next;
	unsigned int tablepos;
};

/* Initializes a `hashmap_iter` structure. */
extern void hashmap_iter_init(struct hashmap *map, struct hashmap_iter *iter);

/* Returns the next hashmap_entry, or NULL if there are no more entries. */
extern void *hashmap_iter_next(struct hashmap_iter *iter);

/* Initializes the iterator and returns the first entry, if any. */
static inline void *hashmap_iter_first(struct hashmap *map,
		struct hashmap_iter *iter)
{
	hashmap_iter_init(map, iter);
	return hashmap_iter_next(iter);
}

/*
 * Disable item counting and automatic rehashing when adding/removing items.
 *
 * Normally, the hashmap keeps track of the number of items in the map
 * and uses it to dynamically resize it.  This (both the counting and
 * the resizing) can cause problems when the map is being used by
 * threaded callers (because the hashmap code does not know about the
 * locking strategy used by the threaded callers and therefore, does
 * not know how to protect the "private_size" counter).
 */
static inline void hashmap_disable_item_counting(struct hashmap *map)
{
	map->do_count_items = 0;
}

/*
 * Re-enable item couting when adding/removing items.
 * If counting is currently disabled, it will force count them.
 * It WILL NOT automatically rehash them.
 */
static inline void hashmap_enable_item_counting(struct hashmap *map)
{
	void *item;
	unsigned int n = 0;
	struct hashmap_iter iter;

	if (map->do_count_items)
		return;

	hashmap_iter_init(map, &iter);
	while ((item = hashmap_iter_next(&iter)))
		n++;

	map->do_count_items = 1;
	map->private_size = n;
}

/* String interning */

/*
 * Returns the unique, interned version of the specified string or data,
 * similar to the `String.intern` API in Java and .NET, respectively.
 * Interned strings remain valid for the entire lifetime of the process.
 *
 * Can be used as `[x]strdup()` or `xmemdupz` replacement, except that interned
 * strings / data must not be modified or freed.
 *
 * Interned strings are best used for short strings with high probability of
 * duplicates.
 *
 * Uses a hashmap to store the pool of interned strings.
 */
extern const void *memintern(const void *data, size_t len);
static inline const char *strintern(const char *string)
{
	return memintern(string, strlen(string));
}

#endif
