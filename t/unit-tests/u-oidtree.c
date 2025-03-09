#include "unit-test.h"
#include "lib-oid.h"
#include "oidtree.h"
#include "hash.h"
#include "hex.h"
#include "strvec.h"

static struct oidtree ot;

#define FILL_TREE(tree, ...)                                       \
	do {                                                       \
		const char *hexes[] = { __VA_ARGS__ };             \
		if (fill_tree_loc(tree, hexes, ARRAY_SIZE(hexes))) \
			return;                                    \
	} while (0)

static int fill_tree_loc(struct oidtree *ot, const char *hexes[], size_t n)
{
	for (size_t i = 0; i < n; i++) {
		struct object_id oid;
		cl_parse_any_oid(hexes[i], &oid);
		oidtree_insert(ot, &oid);
	}
	return 0;
}

static void check_contains(struct oidtree *ot, const char *hex, int expected)
{
	struct object_id oid;

	cl_parse_any_oid(hex, &oid);
	cl_assert_equal_i(oidtree_contains(ot, &oid), expected);
}

struct expected_hex_iter {
	size_t i;
	struct strvec expected_hexes;
	const char *query;
};

static enum cb_next check_each_cb(const struct object_id *oid, void *data)
{
	struct expected_hex_iter *hex_iter = data;
	struct object_id expected;

	cl_assert(hex_iter->i < hex_iter->expected_hexes.nr);

	cl_parse_any_oid(hex_iter->expected_hexes.v[hex_iter->i],
			 &expected);
	cl_assert_equal_s(oid_to_hex(oid), oid_to_hex(&expected));
	hex_iter->i += 1;
	return CB_CONTINUE;
}

LAST_ARG_MUST_BE_NULL
static void check_each(struct oidtree *ot, const char *query, ...)
{
	struct object_id oid;
	struct expected_hex_iter hex_iter = { .expected_hexes = STRVEC_INIT,
					      .query = query };
	const char *arg;
	va_list hex_args;

	va_start(hex_args, query);
	while ((arg = va_arg(hex_args, const char *)))
		strvec_push(&hex_iter.expected_hexes, arg);
	va_end(hex_args);

	cl_parse_any_oid(query, &oid);
	oidtree_each(ot, &oid, strlen(query), check_each_cb, &hex_iter);

	if (hex_iter.i != hex_iter.expected_hexes.nr)
		cl_failf("error: could not find some 'object_id's for query ('%s')", query);

	strvec_clear(&hex_iter.expected_hexes);
}

void test_oidtree__initialize(void)
{
	oidtree_init(&ot);
}

void test_oidtree__cleanup(void)
{
	oidtree_clear(&ot);
}

void test_oidtree__contains(void)
{
	FILL_TREE(&ot, "444", "1", "2", "3", "4", "5", "a", "b", "c", "d", "e");
	check_contains(&ot, "44", 0);
	check_contains(&ot, "441", 0);
	check_contains(&ot, "440", 0);
	check_contains(&ot, "444", 1);
	check_contains(&ot, "4440", 1);
	check_contains(&ot, "4444", 0);
}

void test_oidtree__each(void)
{
	FILL_TREE(&ot, "f", "9", "8", "123", "321", "320", "a", "b", "c", "d", "e");
	check_each(&ot, "12300", "123", NULL);
	check_each(&ot, "3211", NULL); /* should not reach callback */
	check_each(&ot, "3210", "321", NULL);
	check_each(&ot, "32100", "321", NULL);
	check_each(&ot, "32", "320", "321", NULL);
}
