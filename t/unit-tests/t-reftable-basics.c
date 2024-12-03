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

int cmd_main(int argc UNUSED, const char *argv[] UNUSED)
{
	if_test ("binary search with binsearch works") {
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

			idx = binsearch(ARRAY_SIZE(haystack),
					&integer_needle_lesseq, &args);
			check_int(idx, ==, testcases[i].expected_idx);
		}
	}

	if_test ("names_length returns size of a NULL-terminated string array") {
		const char *a[] = { "a", "b", NULL };
		check_int(names_length(a), ==, 2);
	}

	if_test ("names_equal compares NULL-terminated string arrays") {
		const char *a[] = { "a", "b", "c", NULL };
		const char *b[] = { "a", "b", "d", NULL };
		const char *c[] = { "a", "b", NULL };

		check(names_equal(a, a));
		check(!names_equal(a, b));
		check(!names_equal(a, c));
	}

	if_test ("parse_names works for basic input") {
		char in1[] = "line\n";
		char in2[] = "a\nb\nc";
		char **out = parse_names(in1, strlen(in1));
		check(out != NULL);
		check_str(out[0], "line");
		check(!out[1]);
		free_names(out);

		out = parse_names(in2, strlen(in2));
		check(out != NULL);
		check_str(out[0], "a");
		check_str(out[1], "b");
		check_str(out[2], "c");
		check(!out[3]);
		free_names(out);
	}

	if_test ("parse_names drops empty string") {
		char in[] = "a\n\nb\n";
		char **out = parse_names(in, strlen(in));
		check(out != NULL);
		check_str(out[0], "a");
		/* simply '\n' should be dropped as empty string */
		check_str(out[1], "b");
		check(!out[2]);
		free_names(out);
	}

	if_test ("common_prefix_size works") {
		struct reftable_buf a = REFTABLE_BUF_INIT;
		struct reftable_buf b = REFTABLE_BUF_INIT;
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
			check(!reftable_buf_addstr(&a, cases[i].a));
			check(!reftable_buf_addstr(&b, cases[i].b));
			check_int(common_prefix_size(&a, &b), ==, cases[i].want);
			reftable_buf_reset(&a);
			reftable_buf_reset(&b);
		}
		reftable_buf_release(&a);
		reftable_buf_release(&b);
	}

	if_test ("put_be24 and get_be24 work") {
		uint32_t in = 0x112233;
		uint8_t dest[3];
		uint32_t out;
		put_be24(dest, in);
		out = get_be24(dest);
		check_int(in, ==, out);
	}

	if_test ("put_be16 and get_be16 work") {
		uint32_t in = 0xfef1;
		uint8_t dest[3];
		uint32_t out;
		put_be16(dest, in);
		out = get_be16(dest);
		check_int(in, ==, out);
	}

	return test_done();
}
