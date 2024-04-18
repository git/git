/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "system.h"

#include "basics.h"
#include "test_framework.h"
#include "reftable-tests.h"

struct integer_needle_lesseq_args {
	int needle;
	int *haystack;
};

static int integer_needle_lesseq(size_t i, void *_args)
{
	struct integer_needle_lesseq_args *args = _args;
	return args->needle <= args->haystack[i];
}

static void test_binsearch(void)
{
	int haystack[] = { 2, 4, 6, 8, 10 };
	struct {
		int needle;
		size_t expected_idx;
	} testcases[] = {
		{-9000, 0},
		{-1, 0},
		{0, 0},
		{2, 0},
		{3, 1},
		{4, 1},
		{7, 3},
		{9, 4},
		{10, 4},
		{11, 5},
		{9000, 5},
	};
	size_t i = 0;

	for (i = 0; i < ARRAY_SIZE(testcases); i++) {
		struct integer_needle_lesseq_args args = {
			.haystack = haystack,
			.needle = testcases[i].needle,
		};
		size_t idx;

		idx = binsearch(ARRAY_SIZE(haystack), &integer_needle_lesseq, &args);
		EXPECT(idx == testcases[i].expected_idx);
	}
}

static void test_names_length(void)
{
	char *a[] = { "a", "b", NULL };
	EXPECT(names_length(a) == 2);
}

static void test_parse_names_normal(void)
{
	char in[] = "a\nb\n";
	char **out = NULL;
	parse_names(in, strlen(in), &out);
	EXPECT(!strcmp(out[0], "a"));
	EXPECT(!strcmp(out[1], "b"));
	EXPECT(!out[2]);
	free_names(out);
}

static void test_parse_names_drop_empty(void)
{
	char in[] = "a\n\n";
	char **out = NULL;
	parse_names(in, strlen(in), &out);
	EXPECT(!strcmp(out[0], "a"));
	EXPECT(!out[1]);
	free_names(out);
}

static void test_common_prefix(void)
{
	struct strbuf s1 = STRBUF_INIT;
	struct strbuf s2 = STRBUF_INIT;
	strbuf_addstr(&s1, "abcdef");
	strbuf_addstr(&s2, "abc");
	EXPECT(common_prefix_size(&s1, &s2) == 3);
	strbuf_release(&s1);
	strbuf_release(&s2);
}

int basics_test_main(int argc, const char *argv[])
{
	RUN_TEST(test_common_prefix);
	RUN_TEST(test_parse_names_normal);
	RUN_TEST(test_parse_names_drop_empty);
	RUN_TEST(test_binsearch);
	RUN_TEST(test_names_length);
	return 0;
}
