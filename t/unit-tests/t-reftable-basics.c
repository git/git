/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "test-lib.h"
#include "reftable/basics.h"

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

	for (size_t i = 0; i < ARRAY_SIZE(testcases); i++) {
		struct integer_needle_lesseq_args args = {
			.haystack = haystack,
			.needle = testcases[i].needle,
		};
		size_t idx;

		idx = binsearch(ARRAY_SIZE(haystack), &integer_needle_lesseq, &args);
		check_int(idx, ==, testcases[i].expected_idx);
	}
}

static void test_names_length(void)
{
	const char *a[] = { "a", "b", NULL };
	check_int(names_length(a), ==, 2);
}

static void test_names_equal(void)
{
	const char *a[] = { "a", "b", "c", NULL };
	const char *b[] = { "a", "b", "d", NULL };
	const char *c[] = { "a", "b", NULL };

	check(names_equal(a, a));
	check(!names_equal(a, b));
	check(!names_equal(a, c));
}

static void test_parse_names_normal(void)
{
	char in1[] = "line\n";
	char in2[] = "a\nb\nc";
	char **out = NULL;
	parse_names(in1, strlen(in1), &out);
	check_str(out[0], "line");
	check(!out[1]);
	free_names(out);

	parse_names(in2, strlen(in2), &out);
	check_str(out[0], "a");
	check_str(out[1], "b");
	check_str(out[2], "c");
	check(!out[3]);
	free_names(out);
}

static void test_parse_names_drop_empty(void)
{
	char in[] = "a\n\nb\n";
	char **out = NULL;
	parse_names(in, strlen(in), &out);
	check_str(out[0], "a");
	/* simply '\n' should be dropped as empty string */
	check_str(out[1], "b");
	check(!out[2]);
	free_names(out);
}

static void test_common_prefix(void)
{
	struct strbuf a = STRBUF_INIT;
	struct strbuf b = STRBUF_INIT;
	struct {
		const char *a, *b;
		int want;
	} cases[] = {
		{"abcdef", "abc", 3},
		{ "abc", "ab", 2 },
		{ "", "abc", 0 },
		{ "abc", "abd", 2 },
		{ "abc", "pqr", 0 },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		strbuf_addstr(&a, cases[i].a);
		strbuf_addstr(&b, cases[i].b);
		check_int(common_prefix_size(&a, &b), ==, cases[i].want);
		strbuf_reset(&a);
		strbuf_reset(&b);
	}
	strbuf_release(&a);
	strbuf_release(&b);
}

static void test_u24_roundtrip(void)
{
	uint32_t in = 0x112233;
	uint8_t dest[3];
	uint32_t out;
	put_be24(dest, in);
	out = get_be24(dest);
	check_int(in, ==, out);
}

static void test_u16_roundtrip(void)
{
	uint32_t in = 0xfef1;
	uint8_t dest[3];
	uint32_t out;
	put_be16(dest, in);
	out = get_be16(dest);
	check_int(in, ==, out);
}

int cmd_main(int argc, const char *argv[])
{
	TEST(test_common_prefix(), "common_prefix_size works");
	TEST(test_parse_names_normal(), "parse_names works for basic input");
	TEST(test_parse_names_drop_empty(), "parse_names drops empty string");
	TEST(test_binsearch(), "binary search with binsearch works");
	TEST(test_names_length(), "names_length retuns size of a NULL-terminated string array");
	TEST(test_names_equal(), "names_equal compares NULL-terminated string arrays");
	TEST(test_u24_roundtrip(), "put_be24 and get_be24 work");
	TEST(test_u16_roundtrip(), "put_be16 and get_be16 work");

	return test_done();
}
