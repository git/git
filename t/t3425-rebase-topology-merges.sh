#!/bin/sh

test_description='rebase topology tests with merges'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

test_revision_subjects () {
	expected="$1"
	shift
	set -- $(git log --format=%s --no-walk=unsorted "$@")
	test "$expected" = "$*"
}

# a---b-----------c
#      \           \
#       d-------e   \
#        \       \   \
#         n---o---w---v
#              \
#               z
test_expect_success 'setup of non-linear-history' '
	test_commit a &&
	test_commit b &&
	test_commit c &&
	git checkout b &&
	test_commit d &&
	test_commit e &&

	git checkout c &&
	test_commit g &&
	revert h g &&
	git checkout d &&
	cherry_pick gp g &&
	test_commit i &&
	git checkout b &&
	test_commit f &&

	git checkout d &&
	test_commit n &&
	test_commit o &&
	test_merge w e &&
	test_merge v c &&
	git checkout o &&
	test_commit z
'

test_run_rebase () {
	result=$1
	shift
	test_expect_$result "rebase $* after merge from upstream" "
		reset_rebase &&
		git rebase $* e w &&
		test_cmp_rev e HEAD~2 &&
		test_linear_range 'n o' e..
	"
}
test_run_rebase success --apply
test_run_rebase success -m
test_run_rebase success -i

test_run_rebase () {
	result=$1
	shift
	expected=$1
	shift
	test_expect_$result "rebase $* of non-linear history is linearized in place" "
		reset_rebase &&
		git rebase $* d w &&
		test_cmp_rev d HEAD~3 &&
		test_linear_range "\'"$expected"\'" d..
	"
}
test_run_rebase success 'n o e' --apply
test_run_rebase success 'n o e' -m
test_run_rebase success 'n o e' -i

test_run_rebase () {
	result=$1
	shift
	expected=$1
	shift
	test_expect_$result "rebase $* of non-linear history is linearized upstream" "
		reset_rebase &&
		git rebase $* c w &&
		test_cmp_rev c HEAD~4 &&
		test_linear_range "\'"$expected"\'" c..
	"
}
test_run_rebase success 'd n o e' --apply
test_run_rebase success 'd n o e' -m
test_run_rebase success 'd n o e' -i

test_run_rebase () {
	result=$1
	shift
	expected=$1
	shift
	test_expect_$result "rebase $* of non-linear history with merges after upstream merge is linearized" "
		reset_rebase &&
		git rebase $* c v &&
		test_cmp_rev c HEAD~4 &&
		test_linear_range "\'"$expected"\'" c..
	"
}
test_run_rebase success 'd n o e' --apply
test_run_rebase success 'd n o e' -m
test_run_rebase success 'd n o e' -i

test_done
