#!/bin/sh

test_description='basic rebase topology tests'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

# a---b---c
#      \
#       d---e
test_expect_success 'setup' '
	test_commit a &&
	test_commit b &&
	test_commit c &&
	git checkout b &&
	test_commit d &&
	test_commit e
'

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "simple rebase $*" "
		reset_rebase &&
		git rebase $* c e &&
		test_cmp_rev c HEAD~2 &&
		test_linear_range 'd e' c..
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_run_rebase success -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* is no-op if upstream is an ancestor" "
		reset_rebase &&
		git rebase $* b e &&
		test_cmp_rev e HEAD
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_run_rebase success -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* -f rewrites even if upstream is an ancestor" "
		reset_rebase &&
		git rebase $* -f b e &&
		! test_cmp_rev e HEAD &&
		test_cmp_rev b HEAD~2 &&
		test_linear_range 'd e' b..
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_run_rebase failure -p

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* fast-forwards from ancestor of upstream" "
		reset_rebase &&
		git rebase $* e b &&
		test_cmp_rev e HEAD
	"
}
test_run_rebase success ''
test_run_rebase success -m
test_run_rebase success -i
test_run_rebase success -p

test_done
