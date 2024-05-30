#include "test-lib.h"
#include "strbuf.h"
#include "strvec.h"

#define check_strvec(vec, ...) \
	check_strvec_loc(TEST_LOCATION(), vec, __VA_ARGS__)
LAST_ARG_MUST_BE_NULL
static void check_strvec_loc(const char *loc, struct strvec *vec, ...)
{
	va_list ap;
	size_t nr = 0;

	va_start(ap, vec);
	while (1) {
		const char *str = va_arg(ap, const char *);
		if (!str)
			break;

		if (!check_uint(vec->nr, >, nr) ||
		    !check_uint(vec->alloc, >, nr) ||
		    !check_str(vec->v[nr], str)) {
			struct strbuf msg = STRBUF_INIT;
			strbuf_addf(&msg, "strvec index %"PRIuMAX, (uintmax_t) nr);
			test_assert(loc, msg.buf, 0);
			strbuf_release(&msg);
			va_end(ap);
			return;
		}

		nr++;
	}
	va_end(ap);

	check_uint(vec->nr, ==, nr);
	check_uint(vec->alloc, >=, nr);
	check_pointer_eq(vec->v[nr], NULL);
}

static void t_static_init(void)
{
	struct strvec vec = STRVEC_INIT;
	check_pointer_eq(vec.v, empty_strvec);
	check_uint(vec.nr, ==, 0);
	check_uint(vec.alloc, ==, 0);
}

static void t_dynamic_init(void)
{
	struct strvec vec;
	strvec_init(&vec);
	check_pointer_eq(vec.v, empty_strvec);
	check_uint(vec.nr, ==, 0);
	check_uint(vec.alloc, ==, 0);
}

static void t_clear(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_push(&vec, "foo");
	strvec_clear(&vec);
	check_pointer_eq(vec.v, empty_strvec);
	check_uint(vec.nr, ==, 0);
	check_uint(vec.alloc, ==, 0);
}

static void t_push(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_push(&vec, "foo");
	check_strvec(&vec, "foo", NULL);

	strvec_push(&vec, "bar");
	check_strvec(&vec, "foo", "bar", NULL);

	strvec_clear(&vec);
}

static void t_pushf(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushf(&vec, "foo: %d", 1);
	check_strvec(&vec, "foo: 1", NULL);
	strvec_clear(&vec);
}

static void t_pushl(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	check_strvec(&vec, "foo", "bar", "baz", NULL);
	strvec_clear(&vec);
}

static void t_pushv(void)
{
	const char *strings[] = {
		"foo", "bar", "baz", NULL,
	};
	struct strvec vec = STRVEC_INIT;

	strvec_pushv(&vec, strings);
	check_strvec(&vec, "foo", "bar", "baz", NULL);

	strvec_clear(&vec);
}

static void t_replace_at_head(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_replace(&vec, 0, "replaced");
	check_strvec(&vec, "replaced", "bar", "baz", NULL);
	strvec_clear(&vec);
}

static void t_replace_at_tail(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_replace(&vec, 2, "replaced");
	check_strvec(&vec, "foo", "bar", "replaced", NULL);
	strvec_clear(&vec);
}

static void t_replace_in_between(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_replace(&vec, 1, "replaced");
	check_strvec(&vec, "foo", "replaced", "baz", NULL);
	strvec_clear(&vec);
}

static void t_replace_with_substring(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushl(&vec, "foo", NULL);
	strvec_replace(&vec, 0, vec.v[0] + 1);
	check_strvec(&vec, "oo", NULL);
	strvec_clear(&vec);
}

static void t_remove_at_head(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_remove(&vec, 0);
	check_strvec(&vec, "bar", "baz", NULL);
	strvec_clear(&vec);
}

static void t_remove_at_tail(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_remove(&vec, 2);
	check_strvec(&vec, "foo", "bar", NULL);
	strvec_clear(&vec);
}

static void t_remove_in_between(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_remove(&vec, 1);
	check_strvec(&vec, "foo", "baz", NULL);
	strvec_clear(&vec);
}

static void t_pop_empty_array(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pop(&vec);
	check_strvec(&vec, NULL);
	strvec_clear(&vec);
}

static void t_pop_non_empty_array(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_pop(&vec);
	check_strvec(&vec, "foo", "bar", NULL);
	strvec_clear(&vec);
}

static void t_split_empty_string(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_split(&vec, "");
	check_strvec(&vec, NULL);
	strvec_clear(&vec);
}

static void t_split_single_item(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_split(&vec, "foo");
	check_strvec(&vec, "foo", NULL);
	strvec_clear(&vec);
}

static void t_split_multiple_items(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_split(&vec, "foo bar baz");
	check_strvec(&vec, "foo", "bar", "baz", NULL);
	strvec_clear(&vec);
}

static void t_split_whitespace_only(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_split(&vec, " \t\n");
	check_strvec(&vec, NULL);
	strvec_clear(&vec);
}

static void t_split_multiple_consecutive_whitespaces(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_split(&vec, "foo\n\t bar");
	check_strvec(&vec, "foo", "bar", NULL);
	strvec_clear(&vec);
}

static void t_detach(void)
{
	struct strvec vec = STRVEC_INIT;
	const char **detached;

	strvec_push(&vec, "foo");

	detached = strvec_detach(&vec);
	check_str(detached[0], "foo");
	check_pointer_eq(detached[1], NULL);

	check_pointer_eq(vec.v, empty_strvec);
	check_uint(vec.nr, ==, 0);
	check_uint(vec.alloc, ==, 0);

	free((char *) detached[0]);
	free(detached);
}

int cmd_main(int argc, const char **argv)
{
	TEST(t_static_init(), "static initialization");
	TEST(t_dynamic_init(), "dynamic initialization");
	TEST(t_clear(), "clear");
	TEST(t_push(), "push");
	TEST(t_pushf(), "pushf");
	TEST(t_pushl(), "pushl");
	TEST(t_pushv(), "pushv");
	TEST(t_replace_at_head(), "replace at head");
	TEST(t_replace_in_between(), "replace in between");
	TEST(t_replace_at_tail(), "replace at tail");
	TEST(t_replace_with_substring(), "replace with substring");
	TEST(t_remove_at_head(), "remove at head");
	TEST(t_remove_in_between(), "remove in between");
	TEST(t_remove_at_tail(), "remove at tail");
	TEST(t_pop_empty_array(), "pop with empty array");
	TEST(t_pop_non_empty_array(), "pop with non-empty array");
	TEST(t_split_empty_string(), "split empty string");
	TEST(t_split_single_item(), "split single item");
	TEST(t_split_multiple_items(), "split multiple items");
	TEST(t_split_whitespace_only(), "split whitespace only");
	TEST(t_split_multiple_consecutive_whitespaces(), "split multiple consecutive whitespaces");
	TEST(t_detach(), "detach");
	return test_done();
}
