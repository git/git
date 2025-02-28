#define USE_THE_REPOSITORY_VARIABLE

#include "unit-test.h"
#include "lib-oid.h"
#include "oid-array.h"
#include "hex.h"

static void fill_array(struct oid_array *array, const char *hexes[], size_t n)
{
	for (size_t i = 0; i < n; i++) {
		struct object_id oid;

		cl_parse_any_oid(hexes[i], &oid);
		oid_array_append(array, &oid);
	}
	cl_assert_equal_i(array->nr, n);
}

static int add_to_oid_array(const struct object_id *oid, void *data)
{
	struct oid_array *array = data;

	oid_array_append(array, oid);
	return 0;
}

static void t_enumeration(const char **input_args, size_t input_sz,
			  const char **expect_args, size_t expect_sz)
{
	struct oid_array input = OID_ARRAY_INIT, expect = OID_ARRAY_INIT,
			 actual = OID_ARRAY_INIT;
	size_t i;

	fill_array(&input, input_args, input_sz);
	fill_array(&expect, expect_args, expect_sz);

	oid_array_for_each_unique(&input, add_to_oid_array, &actual);
	cl_assert_equal_i(actual.nr, expect.nr);

	for (i = 0; i < actual.nr; i++)
		cl_assert(oideq(&actual.oid[i], &expect.oid[i]));

	oid_array_clear(&actual);
	oid_array_clear(&input);
	oid_array_clear(&expect);
}

#define TEST_ENUMERATION(input, expect)                                     \
	t_enumeration(input, ARRAY_SIZE(input), expect, ARRAY_SIZE(expect));

static void t_lookup(const char **input_hexes, size_t n, const char *query_hex,
		     int lower_bound, int upper_bound)
{
	struct oid_array array = OID_ARRAY_INIT;
	struct object_id oid_query;
	int ret;

	cl_parse_any_oid(query_hex, &oid_query);
	fill_array(&array, input_hexes, n);
	ret = oid_array_lookup(&array, &oid_query);

	cl_assert(ret <= upper_bound);
	cl_assert(ret >= lower_bound);

	oid_array_clear(&array);
}

#define TEST_LOOKUP(input_hexes, query, lower_bound, upper_bound) \
	t_lookup(input_hexes, ARRAY_SIZE(input_hexes), query,      \
		      lower_bound, upper_bound);

void test_oid_array__initialize(void)
{
	/* The hash algo is used by oid_array_lookup() internally */
	int algo = cl_setup_hash_algo();
	repo_set_hash_algo(the_repository, algo);
}

static const char *arr_input[] = { "88", "44", "aa", "55" };
static const char *arr_input_dup[] = { "88", "44", "aa", "55",
				       "88", "44", "aa", "55",
				       "88", "44", "aa", "55" };
static const char *res_sorted[] = { "44", "55", "88", "aa" };

void test_oid_array__enumerate_unique(void)
{
	TEST_ENUMERATION(arr_input, res_sorted);
}

void test_oid_array__enumerate_duplicate(void)
{
	TEST_ENUMERATION(arr_input_dup, res_sorted);
}

void test_oid_array__lookup(void)
{
	TEST_LOOKUP(arr_input, "55", 1, 1);
}

void test_oid_array__lookup_non_existent(void)
{
	TEST_LOOKUP(arr_input, "33", INT_MIN, -1);
}

void test_oid_array__lookup_duplicates(void)
{
	TEST_LOOKUP(arr_input_dup, "55", 3, 5);
}

void test_oid_array__lookup_non_existent_dup(void)
{
	TEST_LOOKUP(arr_input_dup, "66", INT_MIN, -1);
}

void test_oid_array__lookup_almost_dup(void)
{
	const char *nearly_55;

	nearly_55 = cl_setup_hash_algo() == GIT_HASH_SHA1 ?
			"5500000000000000000000000000000000000001" :
			"5500000000000000000000000000000000000000000000000000000000000001";

	TEST_LOOKUP(((const char *[]){ "55", nearly_55 }), "55", 0, 0);
}

void test_oid_array__lookup_single_dup(void)
{
	TEST_LOOKUP(((const char *[]){ "55", "55" }), "55", 0, 1);
}
