#include "unit-test.h"
#include "strbuf.h"
#include "strvec.h"

#define check_strvec(vec, ...) \
	do { \
		const char *expect[] = { __VA_ARGS__ }; \
		size_t expect_len = ARRAY_SIZE(expect); \
		cl_assert(expect_len > 0); \
		cl_assert_equal_p(expect[expect_len - 1], NULL); \
		cl_assert_equal_i((vec)->nr, expect_len - 1); \
		cl_assert((vec)->nr <= (vec)->alloc); \
		for (size_t i = 0; i < expect_len; i++) \
			cl_assert_equal_s((vec)->v[i], expect[i]); \
	} while (0)

void test_strvec__init(void)
{
	struct strvec vec = STRVEC_INIT;

	cl_assert_equal_p(vec.v, empty_strvec);
	cl_assert_equal_i(vec.nr, 0);
	cl_assert_equal_i(vec.alloc, 0);
}

void test_strvec__dynamic_init(void)
{
	struct strvec vec;

	strvec_init(&vec);
	cl_assert_equal_p(vec.v, empty_strvec);
	cl_assert_equal_i(vec.nr, 0);
	cl_assert_equal_i(vec.alloc, 0);
}

void test_strvec__clear(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_push(&vec, "foo");
	strvec_clear(&vec);
	cl_assert_equal_p(vec.v, empty_strvec);
	cl_assert_equal_i(vec.nr, 0);
	cl_assert_equal_i(vec.alloc, 0);
}

void test_strvec__push(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_push(&vec, "foo");
	check_strvec(&vec, "foo", NULL);

	strvec_push(&vec, "bar");
	check_strvec(&vec, "foo", "bar", NULL);

	strvec_clear(&vec);
}

void test_strvec__pushf(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pushf(&vec, "foo: %d", 1);
	check_strvec(&vec, "foo: 1", NULL);
	strvec_clear(&vec);
}

void test_strvec__pushl(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	check_strvec(&vec, "foo", "bar", "baz", NULL);
	strvec_clear(&vec);
}

void test_strvec__pushv(void)
{
	const char *strings[] = {
		"foo", "bar", "baz", NULL,
	};
	struct strvec vec = STRVEC_INIT;

	strvec_pushv(&vec, strings);
	check_strvec(&vec, "foo", "bar", "baz", NULL);

	strvec_clear(&vec);
}

void test_strvec__splice_just_initialized_strvec(void)
{
	struct strvec vec = STRVEC_INIT;
	const char *replacement[] = { "foo" };

	strvec_splice(&vec, 0, 0, replacement, ARRAY_SIZE(replacement));
	check_strvec(&vec, "foo", NULL);
	strvec_clear(&vec);
}

void test_strvec__splice_with_same_size_replacement(void)
{
	struct strvec vec = STRVEC_INIT;
	const char *replacement[] = { "1" };

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_splice(&vec, 1, 1, replacement, ARRAY_SIZE(replacement));
	check_strvec(&vec, "foo", "1", "baz", NULL);
	strvec_clear(&vec);
}

void test_strvec__splice_with_smaller_replacement(void)
{
	struct strvec vec = STRVEC_INIT;
	const char *replacement[] = { "1" };

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_splice(&vec, 1, 2, replacement, ARRAY_SIZE(replacement));
	check_strvec(&vec, "foo", "1", NULL);
	strvec_clear(&vec);
}

void test_strvec__splice_with_bigger_replacement(void)
{
	struct strvec vec = STRVEC_INIT;
	const char *replacement[] = { "1", "2", "3" };

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_splice(&vec, 0, 2, replacement, ARRAY_SIZE(replacement));
	check_strvec(&vec, "1", "2", "3", "baz", NULL);
	strvec_clear(&vec);
}

void test_strvec__splice_with_empty_replacement(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_splice(&vec, 0, 2, NULL, 0);
	check_strvec(&vec, "baz", NULL);
	strvec_clear(&vec);
}

void test_strvec__splice_with_empty_original(void)
{
	struct strvec vec = STRVEC_INIT;
	const char *replacement[] = { "1", "2" };

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_splice(&vec, 1, 0, replacement, ARRAY_SIZE(replacement));
	check_strvec(&vec, "foo", "1", "2", "bar", "baz", NULL);
	strvec_clear(&vec);
}

void test_strvec__splice_at_tail(void)
{
	struct strvec vec = STRVEC_INIT;
	const char *replacement[] = { "1", "2" };

	strvec_pushl(&vec, "foo", "bar", NULL);
	strvec_splice(&vec, 2, 0, replacement, ARRAY_SIZE(replacement));
	check_strvec(&vec, "foo", "bar", "1", "2", NULL);
	strvec_clear(&vec);
}

void test_strvec__replace_at_head(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_replace(&vec, 0, "replaced");
	check_strvec(&vec, "replaced", "bar", "baz", NULL);
	strvec_clear(&vec);
}

void test_strvec__replace_at_tail(void)
{
	struct strvec vec = STRVEC_INIT;
	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_replace(&vec, 2, "replaced");
	check_strvec(&vec, "foo", "bar", "replaced", NULL);
	strvec_clear(&vec);
}

void test_strvec__replace_in_between(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_replace(&vec, 1, "replaced");
	check_strvec(&vec, "foo", "replaced", "baz", NULL);
	strvec_clear(&vec);
}

void test_strvec__replace_with_substring(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pushl(&vec, "foo", NULL);
	strvec_replace(&vec, 0, vec.v[0] + 1);
	check_strvec(&vec, "oo", NULL);
	strvec_clear(&vec);
}

void test_strvec__remove_at_head(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_remove(&vec, 0);
	check_strvec(&vec, "bar", "baz", NULL);
	strvec_clear(&vec);
}

void test_strvec__remove_at_tail(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_remove(&vec, 2);
	check_strvec(&vec, "foo", "bar", NULL);
	strvec_clear(&vec);
}

void test_strvec__remove_in_between(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_remove(&vec, 1);
	check_strvec(&vec, "foo", "baz", NULL);
	strvec_clear(&vec);
}

void test_strvec__pop_empty_array(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pop(&vec);
	check_strvec(&vec, NULL);
	strvec_clear(&vec);
}

void test_strvec__pop_non_empty_array(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_pushl(&vec, "foo", "bar", "baz", NULL);
	strvec_pop(&vec);
	check_strvec(&vec, "foo", "bar", NULL);
	strvec_clear(&vec);
}

void test_strvec__split_empty_string(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_split(&vec, "");
	check_strvec(&vec, NULL);
	strvec_clear(&vec);
}

void test_strvec__split_single_item(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_split(&vec, "foo");
	check_strvec(&vec, "foo", NULL);
	strvec_clear(&vec);
}

void test_strvec__split_multiple_items(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_split(&vec, "foo bar baz");
	check_strvec(&vec, "foo", "bar", "baz", NULL);
	strvec_clear(&vec);
}

void test_strvec__split_whitespace_only(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_split(&vec, " \t\n");
	check_strvec(&vec, NULL);
	strvec_clear(&vec);
}

void test_strvec__split_multiple_consecutive_whitespaces(void)
{
	struct strvec vec = STRVEC_INIT;

	strvec_split(&vec, "foo\n\t bar");
	check_strvec(&vec, "foo", "bar", NULL);
	strvec_clear(&vec);
}

void test_strvec__detach(void)
{
	struct strvec vec = STRVEC_INIT;
	const char **detached;

	strvec_push(&vec, "foo");

	detached = strvec_detach(&vec);
	cl_assert_equal_s(detached[0], "foo");
	cl_assert_equal_p(detached[1], NULL);

	cl_assert_equal_p(vec.v, empty_strvec);
	cl_assert_equal_i(vec.nr, 0);
	cl_assert_equal_i(vec.alloc, 0);

	free((char *) detached[0]);
	free(detached);
}
