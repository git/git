#include "unit-test.h"
#include "list-objects-filter-options.h"
#include "strbuf.h"
#include "string-list.h"

/* Helper to test gently_parse_list_objects_filter() */
static void check_gentle_parse(const char *filter_spec,
			       int expect_success,
			       int allow_auto,
			       enum list_objects_filter_choice expected_choice)
{
	struct list_objects_filter_options filter_options = LIST_OBJECTS_FILTER_INIT;
	struct strbuf errbuf = STRBUF_INIT;
	int ret;

	filter_options.allow_auto_filter = allow_auto;

	ret = gently_parse_list_objects_filter(&filter_options, filter_spec, &errbuf);

	if (expect_success) {
		cl_assert_equal_i(ret, 0);
		cl_assert_equal_i(expected_choice, filter_options.choice);
		cl_assert_equal_i(errbuf.len, 0);
	} else {
		cl_assert(ret != 0);
		cl_assert(errbuf.len > 0);
	}

	strbuf_release(&errbuf);
	list_objects_filter_release(&filter_options);
}

void test_list_objects_filter_options__regular_filters(void)
{
	check_gentle_parse("blob:none", 1, 0, LOFC_BLOB_NONE);
	check_gentle_parse("blob:none", 1, 1, LOFC_BLOB_NONE);
	check_gentle_parse("blob:limit=5k", 1, 0, LOFC_BLOB_LIMIT);
	check_gentle_parse("blob:limit=5k", 1, 1, LOFC_BLOB_LIMIT);
	check_gentle_parse("combine:blob:none+tree:0", 1, 0, LOFC_COMBINE);
	check_gentle_parse("combine:blob:none+tree:0", 1, 1, LOFC_COMBINE);
}

void test_list_objects_filter_options__auto_allowed(void)
{
	check_gentle_parse("auto", 1, 1, LOFC_AUTO);
	check_gentle_parse("auto", 0, 0, 0);
}

void test_list_objects_filter_options__combine_auto_fails(void)
{
	check_gentle_parse("combine:auto+blob:none", 0, 1, 0);
	check_gentle_parse("combine:blob:none+auto", 0, 1, 0);
	check_gentle_parse("combine:auto+auto", 0, 1, 0);
}

/* Helper to test list_objects_filter_combine() */
static void check_combine(const char **specs, size_t nr, const char *expected)
{
	struct string_list spec_list = STRING_LIST_INIT_NODUP;
	char *actual;

	for (size_t i = 0; i < nr; i++)
		string_list_append(&spec_list, specs[i]);

	actual = list_objects_filter_combine(&spec_list);

	cl_assert_equal_s(actual, expected);

	free(actual);
	string_list_clear(&spec_list, 0);
}

void test_list_objects_filter_options__combine_helper(void)
{
	const char *empty[] = { NULL };
	const char *one[] = { "blob:none" };
	const char *two[] = { "blob:none", "tree:0" };
	const char *complex[] = { "blob:limit=1k", "object:type=tag" };
	const char *needs_encoding[] = { "blob:none", "combine:tree:0+blob:limit=1k" };

	check_combine(empty, 0, NULL);
	check_combine(one, 1, "blob:none");
	check_combine(two, 2, "combine:blob:none+tree:0");
	check_combine(complex, 2, "combine:blob:limit=1k+object:type=tag");
	check_combine(needs_encoding, 2, "combine:blob:none+combine:tree:0%2bblob:limit=1k");
}
