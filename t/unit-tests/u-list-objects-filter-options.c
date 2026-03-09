#include "unit-test.h"
#include "list-objects-filter-options.h"
#include "strbuf.h"

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
