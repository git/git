#include "unit-test.h"
#include "lib-oid.h"
#include "oidmap.h"
#include "hash.h"
#include "hex.h"

/*
 * Elements we will put in oidmap structs are made of a key: the entry.oid
 * field, which is of type struct object_id, and a value: the name field (could
 * be a refname for example).
 */
struct test_entry {
	struct oidmap_entry entry;
	char name[FLEX_ARRAY];
};

static const char *const key_val[][2] = { { "11", "one" },
					  { "22", "two" },
					  { "33", "three" } };

static void setup(void (*f)(struct oidmap *map))
{
	struct oidmap map = OIDMAP_INIT;

	for (size_t i = 0; i < ARRAY_SIZE(key_val); i++){
		struct test_entry *entry;

		FLEX_ALLOC_STR(entry, name, key_val[i][1]);
		cl_parse_any_oid(key_val[i][0], &entry->entry.oid);
		entry = oidmap_put(&map, entry);
		cl_assert(entry == NULL);
	}

	f(&map);

	oidmap_free(&map, 1);
}

static void t_replace(struct oidmap *map)
{
	struct test_entry *entry, *prev;

	FLEX_ALLOC_STR(entry, name, "un");
	cl_parse_any_oid("11", &entry->entry.oid);
	prev = oidmap_put(map, entry);
	cl_assert(prev != NULL);
	cl_assert_equal_s(prev->name, "one");
	free(prev);

	FLEX_ALLOC_STR(entry, name, "deux");
	cl_parse_any_oid("22", &entry->entry.oid);
	prev = oidmap_put(map, entry);
	cl_assert(prev != NULL);
	cl_assert_equal_s(prev->name, "two");
	free(prev);
}

static void t_get(struct oidmap *map)
{
	struct test_entry *entry;
	struct object_id oid;

	cl_parse_any_oid("22", &oid);
	entry = oidmap_get(map, &oid);
	cl_assert(entry != NULL);
	cl_assert_equal_s(entry->name, "two");

	cl_parse_any_oid("44", &oid);
	cl_assert(oidmap_get(map, &oid) == NULL);

	cl_parse_any_oid("11", &oid);
	entry = oidmap_get(map, &oid);
	cl_assert(entry != NULL);
	cl_assert_equal_s(entry->name, "one");
}

static void t_remove(struct oidmap *map)
{
	struct test_entry *entry;
	struct object_id oid;

	cl_parse_any_oid("11", &oid);
	entry = oidmap_remove(map, &oid);
	cl_assert(entry != NULL);
	cl_assert_equal_s(entry->name, "one");
	cl_assert(oidmap_get(map, &oid) == NULL);
	free(entry);

	cl_parse_any_oid("22", &oid);
	entry = oidmap_remove(map, &oid);
	cl_assert(entry != NULL);
	cl_assert_equal_s(entry->name, "two");
	cl_assert(oidmap_get(map, &oid) == NULL);
	free(entry);

	cl_parse_any_oid("44", &oid);
	cl_assert(oidmap_remove(map, &oid) == NULL);
}

static int key_val_contains(struct test_entry *entry, char seen[])
{
	for (size_t i = 0; i < ARRAY_SIZE(key_val); i++) {
		struct object_id oid;

		cl_parse_any_oid(key_val[i][0], &oid);

		if (oideq(&entry->entry.oid, &oid)) {
			if (seen[i])
				return 2;
			seen[i] = 1;
			return 0;
		}
	}
	return 1;
}

static void t_iterate(struct oidmap *map)
{
	struct oidmap_iter iter;
	struct test_entry *entry;
	char seen[ARRAY_SIZE(key_val)] = { 0 };
	int count = 0;

	oidmap_iter_init(map, &iter);
	while ((entry = oidmap_iter_next(&iter))) {
		int ret;
		cl_assert_equal_i(ret = key_val_contains(entry, seen), 0);
		count++;
	}
	cl_assert_equal_i(count, ARRAY_SIZE(key_val));
	cl_assert_equal_i(hashmap_get_size(&map->map), ARRAY_SIZE(key_val));
}

void test_oidmap__replace(void)
{
	setup(t_replace);
}

void test_oidmap__get(void)
{
	setup(t_get);
}

void test_oidmap__remove(void)
{
	setup(t_remove);
}

void test_oidmap__iterate(void)
{
	setup(t_iterate);
}
