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
	but log -1 --format=%s "$1" >actual &&
	echo "$2" >expect &&
	test_cmp expect actual
}

expect_deleted() {
	test_must_fail but rev-parse --verify "$1"
}

test_expect_success 'set up repo' '
	test_cummit one &&
	test_cummit two &&
	but remote add origin foo.but
'

test_expect_success 'update branch via @{-1}' '
	but branch previous one &&

	but checkout previous &&
	but checkout main &&

	but branch -f @{-1} two &&
	expect_branch previous two
'

test_expect_success 'update branch via local @{upstream}' '
	but branch local one &&
	but branch --set-upstream-to=local &&

	but branch -f @{upstream} two &&
	expect_branch local two
'

test_expect_success 'disallow updating branch via remote @{upstream}' '
	but update-ref refs/remotes/origin/remote one &&
	but branch --set-upstream-to=origin/remote &&

	test_must_fail but branch -f @{upstream} two
'

test_expect_success 'create branch with pseudo-qualified name' '
	but branch refs/heads/qualified two &&
	expect_branch refs/heads/refs/heads/qualified two
'

test_expect_success 'delete branch via @{-1}' '
	but branch previous-del &&

	but checkout previous-del &&
	but checkout main &&

	but branch -D @{-1} &&
	expect_deleted previous-del
'

test_expect_success 'delete branch via local @{upstream}' '
	but branch local-del &&
	but branch --set-upstream-to=local-del &&

	but branch -D @{upstream} &&
	expect_deleted local-del
'

test_expect_success 'delete branch via remote @{upstream}' '
	but update-ref refs/remotes/origin/remote-del two &&
	but branch --set-upstream-to=origin/remote-del &&

	but branch -r -D @{upstream} &&
	expect_deleted origin/remote-del
'

# Note that we create two oddly named local branches here. We want to make
# sure that we do not accidentally delete either of them, even if
# shorten_unambiguous_ref() tweaks the name to avoid ambiguity.
test_expect_success 'delete @{upstream} expansion matches -r option' '
	but update-ref refs/remotes/origin/remote-del two &&
	but branch --set-upstream-to=origin/remote-del &&
	but update-ref refs/heads/origin/remote-del two &&
	but update-ref refs/heads/remotes/origin/remote-del two &&

	test_must_fail but branch -D @{upstream} &&
	expect_branch refs/heads/origin/remote-del two &&
	expect_branch refs/heads/remotes/origin/remote-del two
'

test_expect_success 'disallow deleting remote branch via @{-1}' '
	but update-ref refs/remotes/origin/previous one &&

	but checkout -b origin/previous two &&
	but checkout main &&

	test_must_fail but branch -r -D @{-1} &&
	expect_branch refs/remotes/origin/previous one &&
	expect_branch refs/heads/origin/previous two
'

# The thing we are testing here is that "@" is the real branch refs/heads/@,
# and not refs/heads/HEAD. These tests should not imply that refs/heads/@ is a
# sane thing, but it _is_ technically allowed for now. If we disallow it, these
# can be switched to test_must_fail.
test_expect_success 'create branch named "@"' '
	but branch -f @ one &&
	expect_branch refs/heads/@ one
'

test_expect_success 'delete branch named "@"' '
	but update-ref refs/heads/@ two &&
	but branch -D @ &&
	expect_deleted refs/heads/@
'

test_expect_success 'checkout does not treat remote @{upstream} as a branch' '
	but update-ref refs/remotes/origin/checkout one &&
	but branch --set-upstream-to=origin/checkout &&
	but update-ref refs/heads/origin/checkout two &&
	but update-ref refs/heads/remotes/origin/checkout two &&

	but checkout @{upstream} &&
	expect_branch HEAD one
'

test_done
