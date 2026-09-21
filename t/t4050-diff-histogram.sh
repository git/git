#!/bin/sh

test_description='histogram diff algorithm'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff-alternative.sh

test_diff_frobnitz "histogram"

test_expect_success WITH_BREAKING_CHANGES 'histogram becomes the default diff algorithm since Git 3.0' '
	test_unconfig diff.algorithm &&
	test_must_fail git diff --no-index file1 file2 >output &&
	test_cmp expect output
'

test_diff_unique "histogram"

test_done
