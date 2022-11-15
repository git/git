#!/bin/sh

test_description="Tests performance of cache tree update operations"

. ./perf-lib.sh

test_perf_large_repo
test_checkout_worktree

count=100

test_expect_success 'setup cache tree' '
	git write-tree
'

test_cache_tree () {
	test_perf "$1, $3" "
		for i in \$(test_seq $count)
		do
			test-tool cache-tree $4 $2
		done
	"
}

test_cache_tree_update_functions () {
	test_cache_tree 'no-op' 'control' "$1" "$2"
	test_cache_tree 'prime_cache_tree' 'prime' "$1" "$2"
	test_cache_tree 'cache_tree_update' 'update' "$1" "$2"
}

test_cache_tree_update_functions "clean" ""
test_cache_tree_update_functions "invalidate 2" "--invalidate 2"
test_cache_tree_update_functions "invalidate 50" "--invalidate 50"
test_cache_tree_update_functions "empty" "--empty"

test_done
