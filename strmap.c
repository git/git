#include "git-compat-util.h"
#include "strmap.h"
#include "mem-pool.h"

int cmp_strmap_entry(const void *hashmap_cmp_fn_data,
		     const struct hashmap_entry *entry1,
		     const struct hashmap_entry *entry2,
		     const void *keydata)
{
	const struct strmap_entry *e1, *e2;

	e1 = container_of(entry1, const struct strmap_entry, ent);
	e2 = container_of(entry2, const struct strmap_entry, ent);
	return strcmp(e1->key, e2->key);
}

static struct strmap_entry *find_strmap_entry(struct strmap *map,
					      const char *str)
{
	struct strmap_entry entry;
	hashmap_entry_init(&entry.ent, strhash(str));
	entry.key = str;
	return hashmap_get_entry(&map->map, &entry, ent, NULL);
}

void strmap_init(struct strmap *map)
{
	struct strmap blank = STRMAP_INIT;
	memcpy(map, &blank, sizeof(*map));
}

void strmap_init_with_options(struct strmap *map,
			      struct mem_pool *pool,
			      int strdup_strings)
{
	hashmap_init(&map->map, cmp_strmap_entry, NULL, 0);
	map->pool = pool;
	map->strdup_strings = strdup_strings;
}

static void strmap_free_entries_(struct strmap *map, int free_values)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;

	if (!map)
		return;

	if (!free_values && map->pool)
		/* Memory other than util is owned by and freed with the pool */
		return;

	/*
	 * We need to iterate over the hashmap entries and free
	 * e->key and e->value ourselves; hashmap has no API to
	 * take care of that for us.  Since we're already iterating over
	 * the hashmap, though, might as well free e too and avoid the need
	 * to make some call into the hashmap API to do that.
	 */
	hashmap_for_each_entry(&map->map, &iter, e, ent) {
		if (free_values)
			free(e->value);
		if (!map->pool)
			free(e);
	}
}

void strmap_clear(struct strmap *map, int free_values)
{
	strmap_free_entries_(map, free_values);
	hashmap_clear(&map->map);
}

void strmap_partial_clear(struct strmap *map, int free_values)
{
	strmap_free_entries_(map, free_values);
	hashmap_partial_clear(&map->map);
}

static struct strmap_entry *create_entry(struct strmap *map,
					 const char *str,
					 void *data)
{
	struct strmap_entry *entry;

	if (map->strdup_strings) {
		if (!map->pool) {
			FLEXPTR_ALLOC_STR(entry, key, str);
		} else {
			size_t len = st_add(strlen(str), 1); /* include NUL */
			entry = mem_pool_alloc(map->pool,
					       st_add(sizeof(*entry), len));
			memcpy(entry + 1, str, len);
			entry->key = (void *)(entry + 1);
		}
	} else if (!map->pool) {
		entry = xmalloc(sizeof(*entry));
	} else {
		entry = mem_pool_alloc(map->pool, sizeof(*entry));
	}
	hashmap_entry_init(&entry->ent, strhash(str));
	if (!map->strdup_strings)
		entry->key = str;
	entry->value = data;
	return entry;
}

void *strmap_put(struct strmap *map, const char *str, void *data)
{
	struct strmap_entry *entry = find_strmap_entry(map, str);

	if (entry) {
		void *old = entry->value;
		entry->value = data;
		return old;
	}

	entry = create_entry(map, str, data);
	hashmap_add(&map->map, &entry->ent);
	return NULL;
}

struct strmap_entry *strmap_get_entry(struct strmap *map, const char *str)
{
	return find_strmap_entry(map, str);
}

void *strmap_get(struct strmap *map, const char *str)
{
	struct strmap_entry *entry = find_strmap_entry(map, str);
	return entry ? entry->value : NULL;
}

int strmap_contains(struct strmap *map, const char *str)
{
	return find_strmap_entry(map, str) != NULL;
}

void strmap_remove(struct strmap *map, const char *str, int free_value)
{
	struct strmap_entry entry, *ret;
	hashmap_entry_init(&entry.ent, strhash(str));
	entry.key = str;
	ret = hashmap_remove_entry(&map->map, &entry, ent, NULL);
	if (!ret)
		return;
	if (free_value)
		free(ret->value);
	if (!map->pool)
		free(ret);
}

void strintmap_incr(struct strintmap *map, const char *str, intptr_t amt)
{
	struct strmap_entry *entry = find_strmap_entry(&map->map, str);
	if (entry) {
		intptr_t *whence = (intptr_t*)&entry->value;
		*whence += amt;
	}
	else
		strintmap_set(map, str, map->default_value + amt);
}

int strset_add(struct strset *set, const char *str)
{
	/*
	 * Cannot use strmap_put() because it'll return NULL in both cases:
	 *   - cannot find str: NULL means "not found"
	 *   - does find str: NULL is the value associated with str
	 */
	struct strmap_entry *entry = find_strmap_entry(&set->map, str);

	if (entry)
		return 0;

	entry = create_entry(&set->map, str, NULL);
	hashmap_add(&set->map.map, &entry->ent);
	return 1;
}
