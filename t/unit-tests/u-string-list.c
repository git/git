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

static void t_string_list_split(const char *data, const char *delim, int maxsplit, ...)
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

static void t_string_list_split_f(const char *data, const char *delim,
				  int maxsplit, unsigned flags, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	struct string_list list = STRING_LIST_INIT_DUP;
	va_list ap;
	int len;

	va_start(ap, flags);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	string_list_clear(&list, 0);
	len = string_list_split_f(&list, data, delim, maxsplit, flags);
	cl_assert_equal_i(len, expected_strings.nr);
	t_string_list_equal(&list, &expected_strings);

	string_list_clear(&expected_strings, 0);
	string_list_clear(&list, 0);
}

void test_string_list__split_f(void)
{
	t_string_list_split_f("::foo:bar:baz:", ":", -1, 0,
			      "", "", "foo", "bar", "baz", "", NULL);
	t_string_list_split_f(" foo:bar : baz", ":", -1, STRING_LIST_SPLIT_TRIM,
			      "foo", "bar", "baz", NULL);
	t_string_list_split_f("  a  b c  ", " ", 1, STRING_LIST_SPLIT_TRIM,
			      "a", "b c", NULL);
	t_string_list_split_f("::foo::bar:baz:", ":", -1, STRING_LIST_SPLIT_NONEMPTY,
			      "foo", "bar", "baz", NULL);
	t_string_list_split_f("foo:baz", ":", -1, STRING_LIST_SPLIT_NONEMPTY,
			      "foo", "baz", NULL);
	t_string_list_split_f("foo :: : baz", ":", -1,
			      STRING_LIST_SPLIT_NONEMPTY | STRING_LIST_SPLIT_TRIM,
			      "foo", "baz", NULL);
}

static void t_string_list_split_in_place_f(const char *data_, const char *delim,
					   int maxsplit, unsigned flags, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	struct string_list list = STRING_LIST_INIT_NODUP;
	char *data = xstrdup(data_);
	va_list ap;
	int len;

	va_start(ap, flags);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	string_list_clear(&list, 0);
	len = string_list_split_in_place_f(&list, data, delim, maxsplit, flags);
	cl_assert_equal_i(len, expected_strings.nr);
	t_string_list_equal(&list, &expected_strings);

	free(data);
	string_list_clear(&expected_strings, 0);
	string_list_clear(&list, 0);
}

void test_string_list__split_in_place_f(void)
{
	t_string_list_split_in_place_f("::foo:bar:baz:", ":", -1, 0,
				       "", "", "foo", "bar", "baz", "", NULL);
	t_string_list_split_in_place_f(" foo:bar : baz", ":", -1, STRING_LIST_SPLIT_TRIM,
				       "foo", "bar", "baz", NULL);
	t_string_list_split_in_place_f("  a  b c  ", " ", 1, STRING_LIST_SPLIT_TRIM,
				       "a", "b c", NULL);
	t_string_list_split_in_place_f("::foo::bar:baz:", ":", -1,
				       STRING_LIST_SPLIT_NONEMPTY,
				       "foo", "bar", "baz", NULL);
	t_string_list_split_in_place_f("foo:baz", ":", -1, STRING_LIST_SPLIT_NONEMPTY,
				       "foo", "baz", NULL);
	t_string_list_split_in_place_f("foo :: : baz", ":", -1,
				       STRING_LIST_SPLIT_NONEMPTY | STRING_LIST_SPLIT_TRIM,
				       "foo", "baz", NULL);
}

void test_string_list__split(void)
{
	t_string_list_split("foo:bar:baz", ":", -1, "foo", "bar", "baz", NULL);
	t_string_list_split("foo:bar:baz", ":", 0, "foo:bar:baz", NULL);
	t_string_list_split("foo:bar:baz", ":", 1, "foo", "bar:baz", NULL);
	t_string_list_split("foo:bar:baz", ":", 2, "foo", "bar", "baz", NULL);
	t_string_list_split("foo:bar:", ":", -1, "foo", "bar", "", NULL);
	t_string_list_split("", ":", -1, "", NULL);
	t_string_list_split(":", ":", -1, "", "", NULL);
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

static int prefix_cb(struct string_list_item *item, void *cb_data)
{
	const char *prefix = (const char *)cb_data;
	return starts_with(item->string, prefix);
}

static void t_string_list_filter(struct string_list *list, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	const char *prefix = "y";
	va_list ap;

	va_start(ap, list);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	filter_string_list(list, 0, prefix_cb, (void *)prefix);
	t_string_list_equal(list, &expected_strings);

	string_list_clear(&expected_strings, 0);
}

void test_string_list__filter(void)
{
	struct string_list list = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&list, 0, NULL);
	t_string_list_filter(&list, NULL);

	t_create_string_list_dup(&list, 0, "no", NULL);
	t_string_list_filter(&list, NULL);

	t_create_string_list_dup(&list, 0, "yes", NULL);
	t_string_list_filter(&list, "yes", NULL);

	t_create_string_list_dup(&list, 0, "no", "yes", NULL);
	t_string_list_filter(&list, "yes", NULL);

	t_create_string_list_dup(&list, 0, "yes", "no", NULL);
	t_string_list_filter(&list, "yes", NULL);

	t_create_string_list_dup(&list, 0, "y1", "y2", NULL);
	t_string_list_filter(&list, "y1", "y2", NULL);

	t_create_string_list_dup(&list, 0, "y2", "y1", NULL);
	t_string_list_filter(&list, "y2", "y1", NULL);

	t_create_string_list_dup(&list, 0, "x1", "x2", NULL);
	t_string_list_filter(&list, NULL);

	t_string_list_clear(&list, 0);
}

static void t_string_list_has_string(
	struct string_list *list,
	const char *string,
	int expected)
{
	int has_string = string_list_has_string(list, string);
	cl_assert_equal_i(has_string, expected);
}

void test_string_list__has_string(void)
{
	struct string_list list = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&list, 0, NULL);
	t_string_list_has_string(&list, "", 0);

	t_create_string_list_dup(&list, 0, "a", "b", "c", NULL);
	t_string_list_has_string(&list, "a", 1);
	t_string_list_has_string(&list, "b", 1);
	t_string_list_has_string(&list, "c", 1);
	t_string_list_has_string(&list, "d", 0);

	t_string_list_clear(&list, 0);
}

static void t_string_list_insert(struct string_list *expected_strings, ...)
{
	struct string_list strings_to_insert = STRING_LIST_INIT_DUP;
	struct string_list list = STRING_LIST_INIT_DUP;
	va_list ap;

	va_start(ap, expected_strings);
	t_vcreate_string_list_dup(&strings_to_insert, 0, ap);
	va_end(ap);

	for (size_t i = 0; i < strings_to_insert.nr; i++)
		string_list_insert(&list, strings_to_insert.items[i].string);

	t_string_list_equal(&list, expected_strings);

	string_list_clear(&strings_to_insert, 0);
	string_list_clear(&list, 0);
}

void test_string_list__insert(void)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&expected_strings, 0, NULL);
	t_string_list_insert(&expected_strings, NULL);

	t_create_string_list_dup(&expected_strings, 0, "a", "b", NULL);
	t_string_list_insert(&expected_strings, "b", "a", "a", "b", NULL);

	t_create_string_list_dup(&expected_strings, 0, "a", "b", "c", NULL);
	t_string_list_insert(&expected_strings, "c", "b", "a", "c", "b", NULL);

	t_create_string_list_dup(&expected_strings, 0, "", "a", NULL);
	t_string_list_insert(&expected_strings, "a", "a", "a", "", NULL);

	t_string_list_clear(&expected_strings, 0);
}

static void t_string_list_sort(struct string_list *list, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	va_list ap;

	va_start(ap, list);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	string_list_sort(list);
	t_string_list_equal(list, &expected_strings);

	string_list_clear(&expected_strings, 0);
}

void test_string_list__sort(void)
{
	struct string_list list = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&list, 0, NULL);
	t_string_list_sort(&list, NULL);

	t_create_string_list_dup(&list, 0, "b", "", "a", NULL);
	t_string_list_sort(&list, "", "a", "b", NULL);

	t_create_string_list_dup(&list, 0, "c", "a", "b", "a", NULL);
	t_string_list_sort(&list, "a", "a", "b", "c", NULL);

	t_string_list_clear(&list, 0);
}

static void t_string_list_remove(
	struct string_list *expected_strings,
	struct string_list *list,
	char const *str)
{
	string_list_remove(list, str, 0);
	t_string_list_equal(list, expected_strings);
}

void test_string_list__remove(void)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	struct string_list list = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&expected_strings, 0, NULL);
	t_create_string_list_dup(&list, 0, NULL);
	t_string_list_remove(&expected_strings, &list, "");

	t_create_string_list_dup(&expected_strings, 0, "a", NULL);
	t_create_string_list_dup(&list, 0, "a", "a", NULL);
	t_string_list_remove(&expected_strings, &list, "a");

	t_create_string_list_dup(&expected_strings, 0, "a", "b", "b", NULL);
	t_create_string_list_dup(&list, 0, "a", "b", "b", "c", NULL);
	t_string_list_remove(&expected_strings, &list, "c");

	t_create_string_list_dup(&expected_strings, 0, "a", "b", "d", NULL);
	t_create_string_list_dup(&list, 0, "a", "b", "c", "d", NULL);
	t_string_list_remove(&expected_strings, &list, "c");

	t_create_string_list_dup(&expected_strings, 0, "a", "b", "c", "d", NULL);
	t_create_string_list_dup(&list, 0, "a", "b", "c", "d", NULL);
	t_string_list_remove(&expected_strings, &list, "e");

	t_string_list_clear(&expected_strings, 0);
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

static void t_string_list_sort_u(struct string_list *list, ...)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	va_list ap;

	va_start(ap, list);
	t_vcreate_string_list_dup(&expected_strings, 0, ap);
	va_end(ap);

	string_list_sort_u(list, 0);
	t_string_list_equal(list, &expected_strings);

	string_list_clear(&expected_strings, 0);
}

void test_string_list__sort_u(void)
{
	struct string_list list = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&list, 0, NULL);
	t_string_list_sort_u(&list, NULL);

	t_create_string_list_dup(&list, 0, "", "", "", "", NULL);
	t_string_list_sort_u(&list, "", NULL);

	t_create_string_list_dup(&list, 0, "b", "a", "a", "", NULL);
	t_string_list_sort_u(&list, "", "a", "b", NULL);

	t_create_string_list_dup(&list, 0, "b", "a", "a", "d", "c", "c", NULL);
	t_string_list_sort_u(&list, "a", "b", "c", "d", NULL);

	t_string_list_clear(&list, 0);
}

static void t_string_list_remove_empty_items(
	struct string_list *expected_strings,
	struct string_list *list)
{
	string_list_remove_empty_items(list, 0);
	t_string_list_equal(list, expected_strings);
}

void test_string_list__remove_empty_items(void)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	struct string_list list = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&expected_strings, 0, NULL);
	t_create_string_list_dup(&list, 0, "", "", "", NULL);
	t_string_list_remove_empty_items(&expected_strings, &list);

	t_create_string_list_dup(&expected_strings, 0, "a", "b", NULL);
	t_create_string_list_dup(&list, 0, "a", "", "b", "", NULL);
	t_string_list_remove_empty_items(&expected_strings, &list);

	t_string_list_clear(&expected_strings, 0);
	t_string_list_clear(&list, 0);
}

static void t_string_list_unsorted_string_list_has_string(
	struct string_list *list,
	const char *str, int expected)
{
	int has_string = unsorted_string_list_has_string(list, str);
	cl_assert_equal_i(has_string, expected);
}

void test_string_list__unsorted_string_list_has_string(void)
{
	struct string_list list = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&list, 0, "b", "d", "a", NULL);
	t_string_list_unsorted_string_list_has_string(&list, "a", 1);
	t_string_list_unsorted_string_list_has_string(&list, "b", 1);
	t_string_list_unsorted_string_list_has_string(&list, "c", 0);
	t_string_list_unsorted_string_list_has_string(&list, "d", 1);

	t_string_list_clear(&list, 0);
}

static void t_string_list_unsorted_string_list_delete_item(
	struct string_list *expected_list,
	struct string_list *list,
	int i)
{
	unsorted_string_list_delete_item(list, i, 0);

	t_string_list_equal(list, expected_list);
}

void test_string_list__unsorted_string_list_delete_item(void)
{
	struct string_list expected_strings = STRING_LIST_INIT_DUP;
	struct string_list list = STRING_LIST_INIT_DUP;

	t_create_string_list_dup(&expected_strings, 0, "a", "c", "b", NULL);
	t_create_string_list_dup(&list, 0, "a", "d", "b", "c", NULL);
	t_string_list_unsorted_string_list_delete_item(&expected_strings, &list, 1);

	t_create_string_list_dup(&expected_strings, 0, NULL);
	t_create_string_list_dup(&list, 0, "", NULL);
	t_string_list_unsorted_string_list_delete_item(&expected_strings, &list, 0);

	t_create_string_list_dup(&expected_strings, 0, "a", "d", "c", "b", NULL);
	t_create_string_list_dup(&list, 0,  "a", "d", "c", "b", "d", NULL);
	t_string_list_unsorted_string_list_delete_item(&expected_strings, &list, 4);

	t_string_list_clear(&expected_strings, 0);
	t_string_list_clear(&list, 0);
}
