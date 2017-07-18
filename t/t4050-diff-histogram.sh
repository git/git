#!/bin/sh

test_description='histogram diff algorithm'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff-alternative.sh

test_diff_frobnitz "histogram"

test_diff_unique "histogram"

test_done
