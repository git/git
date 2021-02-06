#!/bin/sh

test_description='checkout can switch to last branch and merge base'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial world hello &&
	git branch other &&
	test_commit --append second world "hello again"
'

test_expect_success '"checkout -" does not work initially' '
	test_must_fail git checkout -
'

test_expect_success 'first branch switch' '
	git checkout other
'

test_expect_success '"checkout -" switches back' '
	git checkout - &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/main"
'

test_expect_success '"checkout -" switches forth' '
	git checkout - &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/other"
'

test_expect_success 'detach HEAD' '
	git checkout $(git rev-parse HEAD)
'

test_expect_success '"checkout -" attaches again' '
	git checkout - &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/other"
'

test_expect_success '"checkout -" detaches again' '
	git checkout - &&
	test "z$(git rev-parse HEAD)" = "z$(git rev-parse other)" &&
	test_must_fail git symbolic-ref HEAD
'

test_expect_success 'more switches' '
	for i in 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1
	do
		git checkout -b branch$i
	done
'

more_switches () {
	for i in 16 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1
	do
		git checkout branch$i
	done
}

test_expect_success 'switch to the last' '
	more_switches &&
	git checkout @{-1} &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/branch2"
'

test_expect_success 'switch to second from the last' '
	more_switches &&
	git checkout @{-2} &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/branch3"
'

test_expect_success 'switch to third from the last' '
	more_switches &&
	git checkout @{-3} &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/branch4"
'

test_expect_success 'switch to fourth from the last' '
	more_switches &&
	git checkout @{-4} &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/branch5"
'

test_expect_success 'switch to twelfth from the last' '
	more_switches &&
	git checkout @{-12} &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/branch13"
'

test_expect_success 'merge base test setup' '
	git checkout -b another other &&
	test_commit --append third world "hello again"
'

test_expect_success 'another...main' '
	git checkout another &&
	git checkout another...main &&
	test "z$(git rev-parse --verify HEAD)" = "z$(git rev-parse --verify main^)"
'

test_expect_success '...main' '
	git checkout another &&
	git checkout ...main &&
	test "z$(git rev-parse --verify HEAD)" = "z$(git rev-parse --verify main^)"
'

test_expect_success 'main...' '
	git checkout another &&
	git checkout main... &&
	test "z$(git rev-parse --verify HEAD)" = "z$(git rev-parse --verify main^)"
'

test_expect_success '"checkout -" works after a rebase A' '
	git checkout main &&
	git checkout other &&
	git rebase main &&
	git checkout - &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/main"
'

test_expect_success '"checkout -" works after a rebase A B' '
	git branch moodle main~1 &&
	git checkout main &&
	git checkout other &&
	git rebase main moodle &&
	git checkout - &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/main"
'

test_expect_success '"checkout -" works after a rebase -i A' '
	git checkout main &&
	git checkout other &&
	git rebase -i main &&
	git checkout - &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/main"
'

test_expect_success '"checkout -" works after a rebase -i A B' '
	git branch foodle main~1 &&
	git checkout main &&
	git checkout other &&
	git rebase main foodle &&
	git checkout - &&
	test "z$(git symbolic-ref HEAD)" = "zrefs/heads/main"
'

test_done
