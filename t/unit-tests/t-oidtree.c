#include "test-lib.h"
#include "lib-oid.h"
#include "oidtree.h"
#include "hash.h"
#include "hex.h"
#include "strvec.h"

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
		if (!check_int(get_oid_arbitrary_hex(hexes[i], &oid), ==, 0))
			return -1;
		oidtree_insert(ot, &oid);
	}
	return 0;
}

static void check_contains(struct oidtree *ot, const char *hex, int expected)
{
	struct object_id oid;

	if (!check_int(get_oid_arbitrary_hex(hex, &oid), ==, 0))
		return;
	if (!check_int(oidtree_contains(ot, &oid), ==, expected))
		test_msg("oid: %s", oid_to_hex(&oid));
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

	if (!check_int(hex_iter->i, <, hex_iter->expected_hexes.nr)) {
		test_msg("error: extraneous callback for query: ('%s'), object_id: ('%s')",
			 hex_iter->query, oid_to_hex(oid));
		return CB_BREAK;
	}

	if (!check_int(get_oid_arbitrary_hex(hex_iter->expected_hexes.v[hex_iter->i],
					     &expected), ==, 0))
		; /* the data is bogus and cannot be used */
	else if (!check(oideq(oid, &expected)))
		test_msg("expected: %s\n       got: %s\n     query: %s",
			 oid_to_hex(&expected), oid_to_hex(oid), hex_iter->query);

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

	if (!check_int(get_oid_arbitrary_hex(query, &oid), ==, 0))
		return;
	oidtree_each(ot, &oid, strlen(query), check_each_cb, &hex_iter);

	if (!check_int(hex_iter.i, ==, hex_iter.expected_hexes.nr))
		test_msg("error: could not find some 'object_id's for query ('%s')", query);
	strvec_clear(&hex_iter.expected_hexes);
}

static void setup(void (*f)(struct oidtree *ot))
{
	struct oidtree ot;

	oidtree_init(&ot);
	f(&ot);
	oidtree_clear(&ot);
}

static void t_contains(struct oidtree *ot)
{
	FILL_TREE(ot, "444", "1", "2", "3", "4", "5", "a", "b", "c", "d", "e");
	check_contains(ot, "44", 0);
	check_contains(ot, "441", 0);
	check_contains(ot, "440", 0);
	check_contains(ot, "444", 1);
	check_contains(ot, "4440", 1);
	check_contains(ot, "4444", 0);
}

static void t_each(struct oidtree *ot)
{
	FILL_TREE(ot, "f", "9", "8", "123", "321", "320", "a", "b", "c", "d", "e");
	check_each(ot, "12300", "123", NULL);
	check_each(ot, "3211", NULL); /* should not reach callback */
	check_each(ot, "3210", "321", NULL);
	check_each(ot, "32100", "321", NULL);
	check_each(ot, "32", "320", "321", NULL);
}

int cmd_main(int argc UNUSED, const char **argv UNUSED)
{
	TEST(setup(t_contains), "oidtree insert and contains works");
	TEST(setup(t_each), "oidtree each works");
	return test_done();
}
