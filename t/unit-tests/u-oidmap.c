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

static struct oidmap map;

void test_oidmap__initialize(void)
{
	oidmap_init(&map, 0);

	for (size_t i = 0; i < ARRAY_SIZE(key_val); i++){
		struct test_entry *entry;

		FLEX_ALLOC_STR(entry, name, key_val[i][1]);
		cl_parse_any_oid(key_val[i][0], &entry->entry.oid);
		cl_assert(oidmap_put(&map, entry) == NULL);
	}
}

void test_oidmap__cleanup(void)
{
	oidmap_clear(&map, 1);
}

void test_oidmap__replace(void)
{
	struct test_entry *entry, *prev;

	FLEX_ALLOC_STR(entry, name, "un");
	cl_parse_any_oid("11", &entry->entry.oid);
	prev = oidmap_put(&map, entry);
	cl_assert(prev != NULL);
	cl_assert_equal_s(prev->name, "one");
	free(prev);

	FLEX_ALLOC_STR(entry, name, "deux");
	cl_parse_any_oid("22", &entry->entry.oid);
	prev = oidmap_put(&map, entry);
	cl_assert(prev != NULL);
	cl_assert_equal_s(prev->name, "two");
	free(prev);
}

void test_oidmap__get(void)
{
	struct test_entry *entry;
	struct object_id oid;

	cl_parse_any_oid("22", &oid);
	entry = oidmap_get(&map, &oid);
	cl_assert(entry != NULL);
	cl_assert_equal_s(entry->name, "two");

	cl_parse_any_oid("44", &oid);
	cl_assert(oidmap_get(&map, &oid) == NULL);

	cl_parse_any_oid("11", &oid);
	entry = oidmap_get(&map, &oid);
	cl_assert(entry != NULL);
	cl_assert_equal_s(entry->name, "one");
}

void test_oidmap__remove(void)
{
	struct test_entry *entry;
	struct object_id oid;

	cl_parse_any_oid("11", &oid);
	entry = oidmap_remove(&map, &oid);
	cl_assert(entry != NULL);
	cl_assert_equal_s(entry->name, "one");
	cl_assert(oidmap_get(&map, &oid) == NULL);
	free(entry);

	cl_parse_any_oid("22", &oid);
	entry = oidmap_remove(&map, &oid);
	cl_assert(entry != NULL);
	cl_assert_equal_s(entry->name, "two");
	cl_assert(oidmap_get(&map, &oid) == NULL);
	free(entry);

	cl_parse_any_oid("44", &oid);
	cl_assert(oidmap_remove(&map, &oid) == NULL);
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

void test_oidmap__iterate(void)
{
	struct oidmap_iter iter;
	struct test_entry *entry;
	char seen[ARRAY_SIZE(key_val)] = { 0 };
	int count = 0;

	oidmap_iter_init(&map, &iter);
	while ((entry = oidmap_iter_next(&iter))) {
		if (key_val_contains(entry, seen) != 0) {
			cl_failf("Unexpected entry: name = %s, oid = %s",
				 entry->name, oid_to_hex(&entry->entry.oid));
		}
		count++;
	}
	cl_assert_equal_i(count, ARRAY_SIZE(key_val));
	cl_assert_equal_i(hashmap_get_size(&map.map), ARRAY_SIZE(key_val));
}
