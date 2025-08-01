/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "unit-test.h"
#include "lib-reftable.h"
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

static void *realloc_stub(void *p UNUSED, size_t size UNUSED)
{
	return NULL;
}

void test_reftable_basics__binsearch(void)
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

		idx = binsearch(ARRAY_SIZE(haystack),
				&integer_needle_lesseq, &args);
		cl_assert_equal_i(idx, testcases[i].expected_idx);
	}
}

void test_reftable_basics__names_length(void)
{
	const char *a[] = { "a", "b", NULL };
	cl_assert_equal_i(names_length(a), 2);
}

void test_reftable_basics__names_equal(void)
{
	const char *a[] = { "a", "b", "c", NULL };
	const char *b[] = { "a", "b", "d", NULL };
	const char *c[] = { "a", "b", NULL };

	cl_assert(names_equal(a, a));
	cl_assert(!names_equal(a, b));
	cl_assert(!names_equal(a, c));
}

void test_reftable_basics__parse_names(void)
{
	char in1[] = "line\n";
	char in2[] = "a\nb\nc";
	char **out = parse_names(in1, strlen(in1));
	cl_assert(out != NULL);
	cl_assert_equal_s(out[0], "line");
	cl_assert(!out[1]);
	free_names(out);

	out = parse_names(in2, strlen(in2));
	cl_assert(out != NULL);
	cl_assert_equal_s(out[0], "a");
	cl_assert_equal_s(out[1], "b");
	cl_assert_equal_s(out[2], "c");
	cl_assert(!out[3]);
	free_names(out);
}

void test_reftable_basics__parse_names_drop_empty_string(void)
{
	char in[] = "a\n\nb\n";
	char **out = parse_names(in, strlen(in));
	cl_assert(out != NULL);
	cl_assert_equal_s(out[0], "a");
	/* simply '\n' should be dropped as empty string */
	cl_assert_equal_s(out[1], "b");
	cl_assert(out[2] == NULL);
	free_names(out);
}

void test_reftable_basics__common_prefix_size(void)
{
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
		cl_assert_equal_i(reftable_buf_addstr(&a, cases[i].a), 0);
		cl_assert_equal_i(reftable_buf_addstr(&b, cases[i].b), 0);
		cl_assert_equal_i(common_prefix_size(&a, &b), cases[i].want);
		reftable_buf_reset(&a);
		reftable_buf_reset(&b);
	}
	reftable_buf_release(&a);
	reftable_buf_release(&b);
}

void test_reftable_basics__put_get_be64(void)
{
	uint64_t in = 0x1122334455667788;
	uint8_t dest[8];
	uint64_t out;
	reftable_put_be64(dest, in);
	out = reftable_get_be64(dest);
	cl_assert(in == out);
}

void test_reftable_basics__put_get_be32(void)
{
	uint32_t in = 0x11223344;
	uint8_t dest[4];
	uint32_t out;
	reftable_put_be32(dest, in);
	out = reftable_get_be32(dest);
	cl_assert_equal_i(in, out);
}

void test_reftable_basics__put_get_be24(void)
{
	uint32_t in = 0x112233;
	uint8_t dest[3];
	uint32_t out;
	reftable_put_be24(dest, in);
	out = reftable_get_be24(dest);
	cl_assert_equal_i(in, out);
}

void test_reftable_basics__put_get_be16(void)
{
	uint32_t in = 0xfef1;
	uint8_t dest[3];
	uint32_t out;
	reftable_put_be16(dest, in);
	out = reftable_get_be16(dest);
	cl_assert_equal_i(in, out);
}

void test_reftable_basics__alloc_grow(void)
{
	int *arr = NULL, *old_arr;
	size_t alloc = 0, old_alloc;

	cl_assert_equal_i(REFTABLE_ALLOC_GROW(arr, 1, alloc), 0);
	cl_assert(arr != NULL);
	cl_assert(alloc >= 1);
	arr[0] = 42;

	old_alloc = alloc;
	old_arr = arr;
	reftable_set_alloc(NULL, realloc_stub, NULL);
	cl_assert(REFTABLE_ALLOC_GROW(arr, old_alloc + 1, alloc));
	cl_assert(arr == old_arr);
	cl_assert_equal_i(alloc, old_alloc);

	old_alloc = alloc;
	reftable_set_alloc(NULL, NULL, NULL);
	cl_assert_equal_i(REFTABLE_ALLOC_GROW(arr, old_alloc + 1, alloc), 0);
	cl_assert(arr != NULL);
	cl_assert(alloc > old_alloc);
	arr[alloc - 1] = 42;

	reftable_free(arr);
}

void test_reftable_basics__alloc_grow_or_null(void)
{
	int *arr = NULL;
	size_t alloc = 0, old_alloc;

	REFTABLE_ALLOC_GROW_OR_NULL(arr, 1, alloc);
	cl_assert(arr != NULL);
	cl_assert(alloc >= 1);
	arr[0] = 42;

	old_alloc = alloc;
	REFTABLE_ALLOC_GROW_OR_NULL(arr, old_alloc + 1, alloc);
	cl_assert(arr != NULL);
	cl_assert(alloc > old_alloc);
	arr[alloc - 1] = 42;

	old_alloc = alloc;
	reftable_set_alloc(NULL, realloc_stub, NULL);
	REFTABLE_ALLOC_GROW_OR_NULL(arr, old_alloc + 1, alloc);
	cl_assert(arr == NULL);
	cl_assert_equal_i(alloc, 0);
	reftable_set_alloc(NULL, NULL, NULL);

	reftable_free(arr);
}
