#include "unit-test.h"
#include "hashmap.h"
#include "strbuf.h"

struct test_entry {
	int padding; /* hashmap entry no longer needs to be the first member */
	struct hashmap_entry ent;
	/* key and value as two \0-terminated strings */
	char key[FLEX_ARRAY];
};

static int test_entry_cmp(const void *cmp_data,
			  const struct hashmap_entry *eptr,
			  const struct hashmap_entry *entry_or_key,
			  const void *keydata)
{
	const unsigned int ignore_case = cmp_data ? *((int *)cmp_data) : 0;
	const struct test_entry *e1, *e2;
	const char *key = keydata;

	e1 = container_of(eptr, const struct test_entry, ent);
	e2 = container_of(entry_or_key, const struct test_entry, ent);

	if (ignore_case)
		return strcasecmp(e1->key, key ? key : e2->key);
	else
		return strcmp(e1->key, key ? key : e2->key);
}

static const char *get_value(const struct test_entry *e)
{
	return e->key + strlen(e->key) + 1;
}

static struct test_entry *alloc_test_entry(const char *key, const char *value,
					   unsigned int ignore_case)
{
	size_t klen = strlen(key);
	size_t vlen = strlen(value);
	unsigned int hash = ignore_case ? strihash(key) : strhash(key);
	struct test_entry *entry = xmalloc(st_add4(sizeof(*entry), klen, vlen, 2));

	hashmap_entry_init(&entry->ent, hash);
	memcpy(entry->key, key, klen + 1);
	memcpy(entry->key + klen + 1, value, vlen + 1);
	return entry;
}

static struct test_entry *get_test_entry(struct hashmap *map, const char *key,
					 unsigned int ignore_case)
{
	return hashmap_get_entry_from_hash(
		map, ignore_case ? strihash(key) : strhash(key), key,
		struct test_entry, ent);
}

static int key_val_contains(const char *key_val[][2], char seen[], size_t n,
			    struct test_entry *entry)
{
	for (size_t i = 0; i < n; i++) {
		if (!strcmp(entry->key, key_val[i][0]) &&
		    !strcmp(get_value(entry), key_val[i][1])) {
			if (seen[i])
				return 2;
			seen[i] = 1;
			return 0;
		}
	}
	return 1;
}

static void setup(void (*f)(struct hashmap *map, unsigned int ignore_case),
		  unsigned int ignore_case)
{
	struct hashmap map = HASHMAP_INIT(test_entry_cmp, &ignore_case);

	f(&map, ignore_case);
	hashmap_clear_and_free(&map, struct test_entry, ent);
}

static void t_replace(struct hashmap *map, unsigned int ignore_case)
{
	struct test_entry *entry;

	entry = alloc_test_entry("key1", "value1", ignore_case);
	cl_assert_equal_p(hashmap_put_entry(map, entry, ent), NULL);

	entry = alloc_test_entry(ignore_case ? "Key1" : "key1", "value2",
				 ignore_case);
	entry = hashmap_put_entry(map, entry, ent);
	cl_assert(entry != NULL);
	cl_assert_equal_s(get_value(entry), "value1");
	free(entry);

	entry = alloc_test_entry("fooBarFrotz", "value3", ignore_case);
	cl_assert_equal_p(hashmap_put_entry(map, entry, ent), NULL);

	entry = alloc_test_entry(ignore_case ? "FOObarFrotz" : "fooBarFrotz",
				 "value4", ignore_case);
	entry = hashmap_put_entry(map, entry, ent);
	cl_assert(entry != NULL);
	cl_assert_equal_s(get_value(entry), "value3");
	free(entry);
}

static void t_get(struct hashmap *map, unsigned int ignore_case)
{
	struct test_entry *entry;
	const char *key_val[][2] = { { "key1", "value1" },
				     { "key2", "value2" },
				     { "fooBarFrotz", "value3" },
				     { ignore_case ? "key4" : "foobarfrotz",
				       "value4" } };
	const char *query[][2] = {
		{ ignore_case ? "Key1" : "key1", "value1" },
		{ ignore_case ? "keY2" : "key2", "value2" },
		{ ignore_case ? "FOObarFrotz" : "fooBarFrotz", "value3" },
		{ ignore_case ? "FOObarFrotz" : "foobarfrotz",
		  ignore_case ? "value3" : "value4" }
	};

	for (size_t i = 0; i < ARRAY_SIZE(key_val); i++) {
		entry = alloc_test_entry(key_val[i][0], key_val[i][1],
					 ignore_case);
		cl_assert_equal_p(hashmap_put_entry(map, entry, ent), NULL);
	}

	for (size_t i = 0; i < ARRAY_SIZE(query); i++) {
		entry = get_test_entry(map, query[i][0], ignore_case);
		cl_assert(entry != NULL);
		cl_assert_equal_s(get_value(entry), query[i][1]);
	}

	cl_assert_equal_p(get_test_entry(map, "notInMap", ignore_case), NULL);
	cl_assert_equal_i(map->tablesize, 64);
	cl_assert_equal_i(hashmap_get_size(map), ARRAY_SIZE(key_val));
}

static void t_add(struct hashmap *map, unsigned int ignore_case)
{
	struct test_entry *entry;
	const char *key_val[][2] = {
		{ "key1", "value1" },
		{ ignore_case ? "Key1" : "key1", "value2" },
		{ "fooBarFrotz", "value3" },
		{ ignore_case ? "FOObarFrotz" : "fooBarFrotz", "value4" }
	};
	const char *query_keys[] = { "key1", ignore_case ? "FOObarFrotz" :
							   "fooBarFrotz" };
	char seen[ARRAY_SIZE(key_val)] = { 0 };

	for (size_t i = 0; i < ARRAY_SIZE(key_val); i++) {
		entry = alloc_test_entry(key_val[i][0], key_val[i][1], ignore_case);
		hashmap_add(map, &entry->ent);
	}

	for (size_t i = 0; i < ARRAY_SIZE(query_keys); i++) {
		int count = 0;
		entry = hashmap_get_entry_from_hash(map,
			ignore_case ? strihash(query_keys[i]) :
				      strhash(query_keys[i]),
			query_keys[i], struct test_entry, ent);

		hashmap_for_each_entry_from(map, entry, ent)
		{
			int ret = key_val_contains(key_val, seen,
						   ARRAY_SIZE(key_val), entry);
			cl_assert_equal_i(ret, 0);
			count++;
		}
		cl_assert_equal_i(count, 2);
	}

	for (size_t i = 0; i < ARRAY_SIZE(seen); i++)
		cl_assert_equal_i(seen[i], 1);

	cl_assert_equal_i(hashmap_get_size(map), ARRAY_SIZE(key_val));
	cl_assert_equal_p(get_test_entry(map, "notInMap", ignore_case), NULL);
}

static void t_remove(struct hashmap *map, unsigned int ignore_case)
{
	struct test_entry *entry, *removed;
	const char *key_val[][2] = { { "key1", "value1" },
				     { "key2", "value2" },
				     { "fooBarFrotz", "value3" } };
	const char *remove[][2] = { { ignore_case ? "Key1" : "key1", "value1" },
				    { ignore_case ? "keY2" : "key2", "value2" } };

	for (size_t i = 0; i < ARRAY_SIZE(key_val); i++) {
		entry = alloc_test_entry(key_val[i][0], key_val[i][1], ignore_case);
		cl_assert_equal_p(hashmap_put_entry(map, entry, ent), NULL);
	}

	for (size_t i = 0; i < ARRAY_SIZE(remove); i++) {
		entry = alloc_test_entry(remove[i][0], "", ignore_case);
		removed = hashmap_remove_entry(map, entry, ent, remove[i][0]);
		cl_assert(removed != NULL);
		cl_assert_equal_s(get_value(removed), remove[i][1]);
		free(entry);
		free(removed);
	}

	entry = alloc_test_entry("notInMap", "", ignore_case);
	cl_assert_equal_p(hashmap_remove_entry(map, entry, ent, "notInMap"), NULL);
	free(entry);

	cl_assert_equal_i(map->tablesize, 64);
	cl_assert_equal_i(hashmap_get_size(map),
			  ARRAY_SIZE(key_val) - ARRAY_SIZE(remove));
}

static void t_iterate(struct hashmap *map, unsigned int ignore_case)
{
	struct test_entry *entry;
	struct hashmap_iter iter;
	const char *key_val[][2] = { { "key1", "value1" },
				     { "key2", "value2" },
				     { "fooBarFrotz", "value3" } };
	char seen[ARRAY_SIZE(key_val)] = { 0 };

	for (size_t i = 0; i < ARRAY_SIZE(key_val); i++) {
		entry = alloc_test_entry(key_val[i][0], key_val[i][1], ignore_case);
		cl_assert_equal_p(hashmap_put_entry(map, entry, ent), NULL);
	}

	hashmap_for_each_entry(map, &iter, entry, ent /* member name */)
	{
		int ret = key_val_contains(key_val, seen,
					   ARRAY_SIZE(key_val),
					   entry);
		cl_assert(ret == 0);
	}

	for (size_t i = 0; i < ARRAY_SIZE(seen); i++)
		cl_assert_equal_i(seen[i], 1);

	cl_assert_equal_i(hashmap_get_size(map), ARRAY_SIZE(key_val));
}

static void t_alloc(struct hashmap *map, unsigned int ignore_case)
{
	struct test_entry *entry, *removed;

	for (int i = 1; i <= 51; i++) {
		char *key = xstrfmt("key%d", i);
		char *value = xstrfmt("value%d", i);
		entry = alloc_test_entry(key, value, ignore_case);
		cl_assert_equal_p(hashmap_put_entry(map, entry, ent), NULL);
		free(key);
		free(value);
	}
	cl_assert_equal_i(map->tablesize, 64);
	cl_assert_equal_i(hashmap_get_size(map), 51);

	entry = alloc_test_entry("key52", "value52", ignore_case);
	cl_assert_equal_p(hashmap_put_entry(map, entry, ent), NULL);
	cl_assert_equal_i(map->tablesize, 256);
	cl_assert_equal_i(hashmap_get_size(map), 52);

	for (int i = 1; i <= 12; i++) {
		char *key = xstrfmt("key%d", i);
		char *value = xstrfmt("value%d", i);

		entry = alloc_test_entry(key, "", ignore_case);
		removed = hashmap_remove_entry(map, entry, ent, key);
		cl_assert(removed != NULL);
		cl_assert_equal_s(value, get_value(removed));
		free(key);
		free(value);
		free(entry);
		free(removed);
	}
	cl_assert_equal_i(map->tablesize, 256);
	cl_assert_equal_i(hashmap_get_size(map), 40);

	entry = alloc_test_entry("key40", "", ignore_case);
	removed = hashmap_remove_entry(map, entry, ent, "key40");
	cl_assert(removed != NULL);
	cl_assert_equal_s("value40", get_value(removed));
	cl_assert_equal_i(map->tablesize, 64);
	cl_assert_equal_i(hashmap_get_size(map), 39);
	free(entry);
	free(removed);
}

void test_hashmap__intern(void)
{
	const char *values[] = { "value1", "Value1", "value2", "value2" };

	for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
		const char *i1 = strintern(values[i]);
		const char *i2 = strintern(values[i]);

		cl_assert_equal_s(i1, values[i]);
		cl_assert(i1 != values[i]);
		cl_assert_equal_p(i1, i2);
	}
}

void test_hashmap__replace_case_sensitive(void)
{
	setup(t_replace, 0);
}

void test_hashmap__replace_case_insensitive(void)
{
	setup(t_replace, 1);
}

void test_hashmap__get_case_sensitive(void)
{
	setup(t_get, 0);
}

void test_hashmap__get_case_insensitive(void)
{
	setup(t_get, 1);
}

void test_hashmap__add_case_sensitive(void)
{
	setup(t_add, 0);
}

void test_hashmap__add_case_insensitive(void)
{
	setup(t_add, 1);
}

void test_hashmap__remove_case_sensitive(void)
{
	setup(t_remove, 0);
}

void test_hashmap__remove_case_insensitive(void)
{
	setup(t_remove, 1);
}

void test_hashmap__iterate_case_sensitive(void)
{
	setup(t_iterate, 0);
}

void test_hashmap__iterate_case_insensitive(void)
{
	setup(t_iterate, 1);
}

void test_hashmap__alloc_case_sensitive(void)
{
	setup(t_alloc, 0);
}

void test_hashmap__alloc_case_insensitive(void)
{
	setup(t_alloc, 1);
}
