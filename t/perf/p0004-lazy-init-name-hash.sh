#!/bin/sh

test_description='Tests multi-threaded lazy_init_name_hash'
. ./perf-lib.sh

test_perf_large_repo
test_checkout_worktree

test_expect_success 'verify both methods build the same hashmaps' '
	test-lazy-init-name-hash --dump --single | sort >out.single &&
	test-lazy-init-name-hash --dump --multi  | sort >out.multi  &&
	test_cmp out.single out.multi
'

test_expect_success 'multithreaded should be faster' '
	test-lazy-init-name-hash --perf >out.perf
'

test_done
