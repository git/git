#!/bin/sh

test_description='histogram diff algorithm'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff-alternative.sh

test_diff_frobnitz "histogram"

test_diff_unique "histogram"

test_done
