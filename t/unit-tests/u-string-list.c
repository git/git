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

static void t_create_string_list_dup(struct string_list *list, int free_util, ...)
{
	va_list ap;

	cl_assert(list->strdup_strings);

	string_list_clear(list, free_util);
	va_start(ap, free_util);
	t_vcreate_string_list_dup(list, free_util, ap);
	va_end(ap);
}

static void t_string_list_clear(struct string_list *list, int free_util)
{
	string_list_clear(list, free_util);
	cl_assert_equal_p(list->items, NULL);
	cl_assert_equal_i(list->nr, 0);
	cl_assert_equal_i(list->alloc, 0);
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

static void t_string_list_split(struct string_list *list, const char *data,
				int delim, int maxsplit, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	va_list ap;
	int len;

	va_start(ap, maxsplit);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	string_list_clear(list, 0);
	len = string_list_split(list, data, delim, maxsplit);
	cl_assert_equal_i(len, expected_strings.nr);
	t_string_list_equal(list, &expected_strings);

	string_list_clear(&expected_strings, 0);
}

void test_string_list__split(void)
{
	struct string_list list = STRING_LIST_INIT_DUP;

	t_string_list_split(&list, "foo:bar:baz", ':', -1, "foo", "bar", "baz", NULL);
	t_string_list_split(&list, "foo:bar:baz", ':', 0, "foo:bar:baz", NULL);
	t_string_list_split(&list, "foo:bar:baz", ':', 1, "foo", "bar:baz", NULL);
	t_string_list_split(&list, "foo:bar:baz", ':', 2, "foo", "bar", "baz", NULL);
	t_string_list_split(&list, "foo:bar:", ':', -1, "foo", "bar", "", NULL);
	t_string_list_split(&list, "", ':', -1, "", NULL);
	t_string_list_split(&list, ":", ':', -1, "", "", NULL);

	t_string_list_clear(&list, 0);
}

static void t_string_list_split_in_place(struct string_list *list, const char *data,
					 const char *delim, int maxsplit, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	char *string = xstrdup(data);
	va_list ap;
	int len;

	va_start(ap, maxsplit);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	string_list_clear(list, 0);
	len = string_list_split_in_place(list, string, delim, maxsplit);
	cl_assert_equal_i(len, expected_strings.nr);
	t_string_list_equal(list, &expected_strings);

	free(string);
	string_list_clear(&expected_strings, 0);
}

void test_string_list__split_in_place(void)
{
	struct string_list list = STRING_LIST_INIT_NODUP;

	t_string_list_split_in_place(&list, "foo:;:bar:;:baz:;:", ":;", -1,
				     "foo", "", "", "bar", "", "", "baz", "", "", "", NULL);
	t_string_list_split_in_place(&list, "foo:;:bar:;:baz", ":;", 0,
				     "foo:;:bar:;:baz", NULL);
	t_string_list_split_in_place(&list, "foo:;:bar:;:baz", ":;", 1,
				     "foo", ";:bar:;:baz", NULL);
	t_string_list_split_in_place(&list, "foo:;:bar:;:baz", ":;", 2,
				     "foo", "", ":bar:;:baz", NULL);
	t_string_list_split_in_place(&list, "foo:;:bar:;:", ":;", -1,
				     "foo", "", "", "bar", "", "", "", NULL);

	t_string_list_clear(&list, 0);
}

static int prefix_cb(struct string_list_item *item, void *cb_data)
{
	const char *prefix = (const char *)cb_data;
	return starts_with(item->string, prefix);
}

static void t_string_list_filter(struct string_list *list,
				 string_list_each_func_t want, void *cb_data, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	va_list ap;

	va_start(ap, cb_data);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	filter_string_list(list, 0, want, cb_data);
	t_string_list_equal(list, &expected_strings);

	string_list_clear(&expected_strings, 0);
}

void test_string_list__filter(void)
{
	struct string_list list = STRING_LIST_INIT_DUP;
	const char *prefix = "y";

	t_create_string_list_dup(&list, 0, NULL);
	t_string_list_filter(&list, prefix_cb, (void*)prefix, NULL);

	t_create_string_list_dup(&list, 0, "no", NULL);
	t_string_list_filter(&list, prefix_cb, (void*)prefix, NULL);

	t_create_string_list_dup(&list, 0, "yes", NULL);
	t_string_list_filter(&list, prefix_cb, (void*)prefix, "yes", NULL);

	t_create_string_list_dup(&list, 0, "no", "yes", NULL);
	t_string_list_filter(&list, prefix_cb, (void*)prefix, "yes", NULL);

	t_create_string_list_dup(&list, 0, "yes", "no", NULL);
	t_string_list_filter(&list, prefix_cb, (void*)prefix, "yes", NULL);

	t_create_string_list_dup(&list, 0, "y1", "y2", NULL);
	t_string_list_filter(&list, prefix_cb, (void*)prefix, "y1", "y2", NULL);

	t_create_string_list_dup(&list, 0, "y2", "y1", NULL);
	t_string_list_filter(&list, prefix_cb, (void*)prefix, "y2", "y1", NULL);

	t_create_string_list_dup(&list, 0, "x1", "x2", NULL);
	t_string_list_filter(&list, prefix_cb, (void*)prefix, NULL);

	t_string_list_clear(&list, 0);
}

static void t_string_list_remove_duplicates(struct string_list *list, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	va_list ap;

	va_start(ap, list);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	string_list_remove_duplicates(list, 0);
	t_string_list_equal(list, &expected_strings);

	string_list_clear(&expected_strings, 0);
}

void test_string_list__remove_duplicates(void)
{
	struct string_list list = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&list, 0, NULL);
	t_string_list_remove_duplicates(&list, NULL);

	t_create_string_list_dup(&list, 0, "", NULL);
	t_string_list_remove_duplicates(&list, "", NULL);

	t_create_string_list_dup(&list, 0, "a", NULL);
	t_string_list_remove_duplicates(&list, "a", NULL);

	t_create_string_list_dup(&list, 0, "a", "a", NULL);
	t_string_list_remove_duplicates(&list, "a", NULL);

	t_create_string_list_dup(&list, 0, "a", "a", "a", NULL);
	t_string_list_remove_duplicates(&list, "a", NULL);

	t_create_string_list_dup(&list, 0, "a", "a", "b", NULL);
	t_string_list_remove_duplicates(&list, "a", "b", NULL);

	t_create_string_list_dup(&list, 0, "a", "b", "b", NULL);
	t_string_list_remove_duplicates(&list, "a", "b", NULL);

	t_create_string_list_dup(&list, 0, "a", "b", "c", NULL);
	t_string_list_remove_duplicates(&list, "a", "b", "c", NULL);

	t_create_string_list_dup(&list, 0, "a", "a", "b", "c", NULL);
	t_string_list_remove_duplicates(&list, "a", "b", "c", NULL);

	t_create_string_list_dup(&list, 0, "a", "b", "b", "c", NULL);
	t_string_list_remove_duplicates(&list, "a", "b", "c", NULL);

	t_create_string_list_dup(&list, 0, "a", "b", "c", "c", NULL);
	t_string_list_remove_duplicates(&list, "a", "b", "c", NULL);

	t_create_string_list_dup(&list, 0, "a", "a", "b", "b", "c", "c", NULL);
	t_string_list_remove_duplicates(&list, "a", "b", "c", NULL);

	t_create_string_list_dup(&list, 0, "a", "a", "a", "b", "b", "b",
				 "c", "c", "c", NULL);
	t_string_list_remove_duplicates(&list, "a", "b", "c", NULL);

	t_string_list_clear(&list, 0);
}
