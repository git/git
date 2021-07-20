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

struct binsearch_args {
	int key;
	int *arr;
};

static int binsearch_func(size_t i, void *void_args)
{
	struct binsearch_args *args = void_args;

	return args->key < args->arr[i];
}

static void test_binsearch(void)
{
	int arr[] = { 2, 4, 6, 8, 10 };
	size_t sz = ARRAY_SIZE(arr);
	struct binsearch_args args = {
		.arr = arr,
	};

	int i = 0;
	for (i = 1; i < 11; i++) {
		int res;
		args.key = i;
		res = binsearch(sz, &binsearch_func, &args);

		if (res < sz) {
			EXPECT(args.key < arr[res]);
			if (res > 0) {
				EXPECT(args.key >= arr[res - 1]);
			}
		} else {
			EXPECT(args.key == 10 || args.key == 11);
		}
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
