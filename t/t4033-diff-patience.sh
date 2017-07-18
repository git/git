#!/bin/sh

test_description='patience diff algorithm'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff-alternative.sh

test_diff_frobnitz "patience"

test_diff_unique "patience"

test_done
