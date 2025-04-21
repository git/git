#include "unit-test.h"
#include "string-list.h"

static void t_check_string_list(struct string_list *list,
				struct string_list *expected_strings)
{
	size_t expect_len = expected_strings->nr;
	cl_assert_equal_i(list->nr, expect_len);
	cl_assert(list->nr <= list->alloc);
	for (size_t i = 0; i < expect_len; i++)
		cl_assert_equal_s(list->items[i].string,
				  expected_strings->items[i].string);
}

static void t_string_list_clear(struct string_list *list, int free_util)
{
	string_list_clear(list, free_util);
	cl_assert_equal_p(list->items, NULL);
	cl_assert_equal_i(list->nr, 0);
	cl_assert_equal_i(list->alloc, 0);
}

static void t_vcreate_string_list_dup(struct string_list *list,
				      int free_util, va_list ap)
{
	const char *arg;

	cl_assert(list->strdup_strings);

	t_string_list_clear(list, free_util);
	while ((arg = va_arg(ap, const char *)))
		string_list_append(list, arg);
}

static void t_create_string_list_dup(struct string_list *list, int free_util, ...)
{
	va_list ap;

	cl_assert(list->strdup_strings);

	t_string_list_clear(list, free_util);
	va_start(ap, free_util);
	t_vcreate_string_list_dup(list, free_util, ap);
	va_end(ap);
}

static void t_string_list_split(const char *data, int delim, int maxsplit,
				struct string_list *expected_strings)
{
	struct string_list list = STRING_LIST_INIT_DUP;
	int len;

	len = string_list_split(&list, data, delim, maxsplit);
	cl_assert_equal_i(len, expected_strings->nr);
	t_check_string_list(&list, expected_strings);

	t_string_list_clear(&list, 0);
}

void test_string_list__split(void)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&expected_strings, 0, "foo", "bar", "baz", NULL);
	t_string_list_split("foo:bar:baz", ':', -1, &expected_strings);

	t_create_string_list_dup(&expected_strings, 0, "foo:bar:baz", NULL);
	t_string_list_split("foo:bar:baz", ':', 0, &expected_strings);

	t_create_string_list_dup(&expected_strings, 0, "foo", "bar:baz", NULL);
	t_string_list_split("foo:bar:baz", ':', 1, &expected_strings);

	t_create_string_list_dup(&expected_strings, 0, "foo", "bar", "baz", NULL);
	t_string_list_split("foo:bar:baz", ':', 2, &expected_strings);

	t_create_string_list_dup(&expected_strings, 0, "foo", "bar", "", NULL);
	t_string_list_split("foo:bar:", ':', -1, &expected_strings);

	t_create_string_list_dup(&expected_strings, 0, "", NULL);
	t_string_list_split("", ':', -1, &expected_strings);

	t_create_string_list_dup(&expected_strings, 0, "", "", NULL);
	t_string_list_split(":", ':', -1, &expected_strings);

	t_string_list_clear(&expected_strings, 0);
}
