#include "unit-test.h"
#include "string-list.h"

static void t_vcreate_string_list_dup(struct string_list *list,
				      int free_util, va_list ap)
{
	const char *arg;

	cl_assert(list->strdup_strings);

	string_list_clear(list, free_util);
	while ((arg = va_arg(ap, const char *)))
		string_list_append(list, arg);
}

static void t_string_list_equal(struct string_list *list,
				struct string_list *expected_strings)
{
	cl_assert_equal_i(list->nr, expected_strings->nr);
	cl_assert(list->nr <= list->alloc);
	for (size_t i = 0; i < expected_strings->nr; i++)
		cl_assert_equal_s(list->items[i].string,
				  expected_strings->items[i].string);
}

static void t_string_list_split(const char *data, int delim, int maxsplit, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	struct string_list list = STRING_LIST_INIT_DUP;
	va_list ap;
	int len;

	va_start(ap, maxsplit);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	string_list_clear(&list, 0);
	len = string_list_split(&list, data, delim, maxsplit);
	cl_assert_equal_i(len, expected_strings.nr);
	t_string_list_equal(&list, &expected_strings);

	string_list_clear(&expected_strings, 0);
	string_list_clear(&list, 0);
}

void test_string_list__split(void)
{
	t_string_list_split("foo:bar:baz", ':', -1, "foo", "bar", "baz", NULL);
	t_string_list_split("foo:bar:baz", ':', 0, "foo:bar:baz", NULL);
	t_string_list_split("foo:bar:baz", ':', 1, "foo", "bar:baz", NULL);
	t_string_list_split("foo:bar:baz", ':', 2, "foo", "bar", "baz", NULL);
	t_string_list_split("foo:bar:", ':', -1, "foo", "bar", "", NULL);
	t_string_list_split("", ':', -1, "", NULL);
	t_string_list_split(":", ':', -1, "", "", NULL);
}

static void t_string_list_split_in_place(const char *data, const char *delim,
					 int maxsplit, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	struct string_list list = STRING_LIST_INIT_NODUP;
	char *string = xstrdup(data);
	va_list ap;
	int len;

	va_start(ap, maxsplit);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	string_list_clear(&list, 0);
	len = string_list_split_in_place(&list, string, delim, maxsplit);
	cl_assert_equal_i(len, expected_strings.nr);
	t_string_list_equal(&list, &expected_strings);

	free(string);
	string_list_clear(&expected_strings, 0);
	string_list_clear(&list, 0);
}

void test_string_list__split_in_place(void)
{
	t_string_list_split_in_place("foo:;:bar:;:baz:;:", ":;", -1,
				     "foo", "", "", "bar", "", "", "baz", "", "", "", NULL);
	t_string_list_split_in_place("foo:;:bar:;:baz", ":;", 0,
				     "foo:;:bar:;:baz", NULL);
	t_string_list_split_in_place("foo:;:bar:;:baz", ":;", 1,
				     "foo", ";:bar:;:baz", NULL);
	t_string_list_split_in_place("foo:;:bar:;:baz", ":;", 2,
				     "foo", "", ":bar:;:baz", NULL);
	t_string_list_split_in_place("foo:;:bar:;:", ":;", -1,
				     "foo", "", "", "bar", "", "", "", NULL);
}
