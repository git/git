/*
 * Generic implementation of hash-based key value mappings.
 */
#include "cache.h"
#include "hashmap.h"

#define FNV32_BASE ((unsigned int) 0x811c9dc5)
#define FNV32_PRIME ((unsigned int) 0x01000193)

unsigned int strhash(const char *str)
{
	unsigned int c, hash = FNV32_BASE;
	while ((c = (unsigned char) *str++))
		hash = (hash * FNV32_PRIME) ^ c;
	return hash;
}

unsigned int strihash(const char *str)
{
	unsigned int c, hash = FNV32_BASE;
	while ((c = (unsigned char) *str++)) {
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		hash = (hash * FNV32_PRIME) ^ c;
	}
	return hash;
}

unsigned int memhash(const void *buf, size_t len)
{
	unsigned int hash = FNV32_BASE;
	unsigned char *ucbuf = (unsigned char *) buf;
	while (len--) {
		unsigned int c = *ucbuf++;
		hash = (hash * FNV32_PRIME) ^ c;
	}
	return hash;
}

unsigned int memihash(const void *buf, size_t len)
{
	unsigned int hash = FNV32_BASE;
	unsigned char *ucbuf = (unsigned char *) buf;
	while (len--) {
		unsigned int c = *ucbuf++;
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		hash = (hash * FNV32_PRIME) ^ c;
	}
	return hash;
}

#define HASHMAP_INITIAL_SIZE 64
/* grow / shrink by 2^2 */
#define HASHMAP_RESIZE_BITS 2
/* load factor in percent */
#define HASHMAP_LOAD_FACTOR 80

static void alloc_table(struct hashmap *map, unsigned int size)
{
	map->tablesize = size;
	map->table = xcalloc(size, sizeof(struct hashmap_entry *));

	/* calculate resize thresholds for new size */
	map->grow_at = (unsigned int) ((uint64_t) size * HASHMAP_LOAD_FACTOR / 100);
	if (size <= HASHMAP_INITIAL_SIZE)
		map->shrink_at = 0;
	else
		/*
		 * The shrink-threshold must be slightly smaller than
		 * (grow-threshold / resize-factor) to prevent erratic resizing,
		 * thus we divide by (resize-factor + 1).
		 */
		map->shrink_at = map->grow_at / ((1 << HASHMAP_RESIZE_BITS) + 1);
}

static inline int entry_equals(const struct hashmap *map,
		const struct hashmap_entry *e1, const struct hashmap_entry *e2,
		const void *keydata)
{
	return (e1 == e2) || (e1->hash == e2->hash && !map->cmpfn(e1, e2, keydata));
}

static inline unsigned int bucket(const struct hashmap *map,
		const struct hashmap_entry *key)
{
	return key->hash & (map->tablesize - 1);
}

static void rehash(struct hashmap *map, unsigned int newsize)
{
	unsigned int i, oldsize = map->tablesize;
	struct hashmap_entry **oldtable = map->table;

	alloc_table(map, newsize);
	for (i = 0; i < oldsize; i++) {
		struct hashmap_entry *e = oldtable[i];
		while (e) {
			struct hashmap_entry *next = e->next;
			unsigned int b = bucket(map, e);
			e->next = map->table[b];
			map->table[b] = e;
			e = next;
		}
	}
	free(oldtable);
}

static inline struct hashmap_entry **find_entry_ptr(const struct hashmap *map,
		const struct hashmap_entry *key, const void *keydata)
{
	struct hashmap_entry **e = &map->table[bucket(map, key)];
	while (*e && !entry_equals(map, *e, key, keydata))
		e = &(*e)->next;
	return e;
}

static int always_equal(const void *unused1, const void *unused2, const void *unused3)
{
	return 0;
}

void hashmap_init(struct hashmap *map, hashmap_cmp_fn equals_function,
		size_t initial_size)
{
	unsigned int size = HASHMAP_INITIAL_SIZE;
	map->size = 0;
	map->cmpfn = equals_function ? equals_function : always_equal;

	/* calculate initial table size and allocate the table */
	initial_size = (unsigned int) ((uint64_t) initial_size * 100
			/ HASHMAP_LOAD_FACTOR);
	while (initial_size > size)
		size <<= HASHMAP_RESIZE_BITS;
	alloc_table(map, size);
}

void hashmap_free(struct hashmap *map, int free_entries)
{
	if (!map || !map->table)
		return;
	if (free_entries) {
		struct hashmap_iter iter;
		struct hashmap_entry *e;
		hashmap_iter_init(map, &iter);
		while ((e = hashmap_iter_next(&iter)))
			free(e);
	}
	free(map->table);
	memset(map, 0, sizeof(*map));
}

void *hashmap_get(const struct hashmap *map, const void *key, const void *keydata)
{
	return *find_entry_ptr(map, key, keydata);
}

void *hashmap_get_next(const struct hashmap *map, const void *entry)
{
	struct hashmap_entry *e = ((struct hashmap_entry *) entry)->next;
	for (; e; e = e->next)
		if (entry_equals(map, entry, e, NULL))
			return e;
	return NULL;
}

void hashmap_add(struct hashmap *map, void *entry)
{
	unsigned int b = bucket(map, entry);

	/* add entry */
	((struct hashmap_entry *) entry)->next = map->table[b];
	map->table[b] = entry;

	/* fix size and rehash if appropriate */
	map->size++;
	if (map->size > map->grow_at)
		rehash(map, map->tablesize << HASHMAP_RESIZE_BITS);
}

void *hashmap_remove(struct hashmap *map, const void *key, const void *keydata)
{
	struct hashmap_entry *old;
	struct hashmap_entry **e = find_entry_ptr(map, key, keydata);
	if (!*e)
		return NULL;

	/* remove existing entry */
	old = *e;
	*e = old->next;
	old->next = NULL;

	/* fix size and rehash if appropriate */
	map->size--;
	if (map->size < map->shrink_at)
		rehash(map, map->tablesize >> HASHMAP_RESIZE_BITS);
	return old;
}

void *hashmap_put(struct hashmap *map, void *entry)
{
	struct hashmap_entry *old = hashmap_remove(map, entry, NULL);
	hashmap_add(map, entry);
	return old;
}

void hashmap_iter_init(struct hashmap *map, struct hashmap_iter *iter)
{
	iter->map = map;
	iter->tablepos = 0;
	iter->next = NULL;
}

void *hashmap_iter_next(struct hashmap_iter *iter)
{
	struct hashmap_entry *current = iter->next;
	for (;;) {
		if (current) {
			iter->next = current->next;
			return current;
		}

		if (iter->tablepos >= iter->map->tablesize)
			return NULL;

		current = iter->map->table[iter->tablepos++];
	}
}

struct pool_entry {
	struct hashmap_entry ent;
	size_t len;
	unsigned char data[FLEX_ARRAY];
};

static int pool_entry_cmp(const struct pool_entry *e1,
			  const struct pool_entry *e2,
			  const unsigned char *keydata)
{
	return e1->data != keydata &&
	       (e1->len != e2->len || memcmp(e1->data, keydata, e1->len));
}

const void *memintern(const void *data, size_t len)
{
	static struct hashmap map;
	struct pool_entry key, *e;

	/* initialize string pool hashmap */
	if (!map.tablesize)
		hashmap_init(&map, (hashmap_cmp_fn) pool_entry_cmp, 0);

	/* lookup interned string in pool */
	hashmap_entry_init(&key, memhash(data, len));
	key.len = len;
	e = hashmap_get(&map, &key, data);
	if (!e) {
		/* not found: create it */
		FLEX_ALLOC_MEM(e, data, data, len);
		hashmap_entry_init(e, key.ent.hash);
		e->len = len;
		hashmap_add(&map, e);
	}
	return e->data;
}
