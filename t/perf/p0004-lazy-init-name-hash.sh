#!/bin/sh

test_description='Tests multi-threaded lazy_init_name_hash'
. ./perf-lib.sh

test_perf_large_repo
test_checkout_worktree

test_expect_success 'verify both methods build the same hashmaps' '
	$GIT_BUILD_DIR/t/helper/test-lazy-init-name-hash$X --dump --single | sort >out.single &&
	$GIT_BUILD_DIR/t/helper/test-lazy-init-name-hash$X --dump --multi  | sort >out.multi  &&
	test_cmp out.single out.multi
'

test_expect_success 'multithreaded should be faster' '
	$GIT_BUILD_DIR/t/helper/test-lazy-init-name-hash$X --perf >out.perf
'

test_done
