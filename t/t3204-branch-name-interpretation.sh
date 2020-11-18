#!/bin/sh

test_description='interpreting exotic branch name arguments

Branch name arguments are usually names which are taken to be inside of
refs/heads/, but we interpret some magic syntax like @{-1}, @{upstream}, etc.
This script aims to check the behavior of those corner cases.
'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

expect_branch() {
	git log -1 --format=%s "$1" >actual &&
	echo "$2" >expect &&
	test_cmp expect actual
}

expect_deleted() {
	test_must_fail git rev-parse --verify "$1"
}

test_expect_success 'set up repo' '
	test_commit one &&
	test_commit two &&
	git remote add origin foo.git
'

test_expect_success 'update branch via @{-1}' '
	git branch previous one &&

	git checkout previous &&
	git checkout main &&

	git branch -f @{-1} two &&
	expect_branch previous two
'

test_expect_success 'update branch via local @{upstream}' '
	git branch local one &&
	git branch --set-upstream-to=local &&

	git branch -f @{upstream} two &&
	expect_branch local two
'

test_expect_success 'disallow updating branch via remote @{upstream}' '
	git update-ref refs/remotes/origin/remote one &&
	git branch --set-upstream-to=origin/remote &&

	test_must_fail git branch -f @{upstream} two
'

test_expect_success 'create branch with pseudo-qualified name' '
	git branch refs/heads/qualified two &&
	expect_branch refs/heads/refs/heads/qualified two
'

test_expect_success 'delete branch via @{-1}' '
	git branch previous-del &&

	git checkout previous-del &&
	git checkout main &&

	git branch -D @{-1} &&
	expect_deleted previous-del
'

test_expect_success 'delete branch via local @{upstream}' '
	git branch local-del &&
	git branch --set-upstream-to=local-del &&

	git branch -D @{upstream} &&
	expect_deleted local-del
'

test_expect_success 'delete branch via remote @{upstream}' '
	git update-ref refs/remotes/origin/remote-del two &&
	git branch --set-upstream-to=origin/remote-del &&

	git branch -r -D @{upstream} &&
	expect_deleted origin/remote-del
'

# Note that we create two oddly named local branches here. We want to make
# sure that we do not accidentally delete either of them, even if
# shorten_unambiguous_ref() tweaks the name to avoid ambiguity.
test_expect_success 'delete @{upstream} expansion matches -r option' '
	git update-ref refs/remotes/origin/remote-del two &&
	git branch --set-upstream-to=origin/remote-del &&
	git update-ref refs/heads/origin/remote-del two &&
	git update-ref refs/heads/remotes/origin/remote-del two &&

	test_must_fail git branch -D @{upstream} &&
	expect_branch refs/heads/origin/remote-del two &&
	expect_branch refs/heads/remotes/origin/remote-del two
'

test_expect_success 'disallow deleting remote branch via @{-1}' '
	git update-ref refs/remotes/origin/previous one &&

	git checkout -b origin/previous two &&
	git checkout main &&

	test_must_fail git branch -r -D @{-1} &&
	expect_branch refs/remotes/origin/previous one &&
	expect_branch refs/heads/origin/previous two
'

# The thing we are testing here is that "@" is the real branch refs/heads/@,
# and not refs/heads/HEAD. These tests should not imply that refs/heads/@ is a
# sane thing, but it _is_ technically allowed for now. If we disallow it, these
# can be switched to test_must_fail.
test_expect_success 'create branch named "@"' '
	git branch -f @ one &&
	expect_branch refs/heads/@ one
'

test_expect_success 'delete branch named "@"' '
	git update-ref refs/heads/@ two &&
	git branch -D @ &&
	expect_deleted refs/heads/@
'

test_expect_success 'checkout does not treat remote @{upstream} as a branch' '
	git update-ref refs/remotes/origin/checkout one &&
	git branch --set-upstream-to=origin/checkout &&
	git update-ref refs/heads/origin/checkout two &&
	git update-ref refs/heads/remotes/origin/checkout two &&

	git checkout @{upstream} &&
	expect_branch HEAD one
'

test_done
