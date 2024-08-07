#!/bin/sh

test_description='test cloning a repository with detached HEAD'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

head_is_detached() {
	git --git-dir=$1/.git rev-parse --verify HEAD &&
	test_must_fail git --git-dir=$1/.git symbolic-ref HEAD
}

test_expect_success 'setup' '
	echo one >file &&
	git add file &&
	git commit -m one &&
	echo two >file &&
	git commit -a -m two &&
	git tag two &&
	echo three >file &&
	git commit -a -m three
'

test_expect_success 'clone repo (detached HEAD points to branch)' '
	git checkout main^0 &&
	git clone "file://$PWD" detached-branch
'
test_expect_success 'cloned HEAD matches' '
	echo three >expect &&
	git --git-dir=detached-branch/.git log -1 --format=%s >actual &&
	test_cmp expect actual
'
test_expect_failure 'cloned HEAD is detached' '
	head_is_detached detached-branch
'

test_expect_success 'clone repo (detached HEAD points to tag)' '
	git checkout two^0 &&
	git clone "file://$PWD" detached-tag
'
test_expect_success 'cloned HEAD matches' '
	echo two >expect &&
	git --git-dir=detached-tag/.git log -1 --format=%s >actual &&
	test_cmp expect actual
'
test_expect_success 'cloned HEAD is detached' '
	head_is_detached detached-tag
'

test_expect_success 'clone repo (detached HEAD points to history)' '
	git checkout two^ &&
	git clone "file://$PWD" detached-history
'
test_expect_success 'cloned HEAD matches' '
	echo one >expect &&
	git --git-dir=detached-history/.git log -1 --format=%s >actual &&
	test_cmp expect actual
'
test_expect_success 'cloned HEAD is detached' '
	head_is_detached detached-history
'

test_expect_success 'clone repo (orphan detached HEAD)' '
	git checkout main^0 &&
	echo four >file &&
	git commit -a -m four &&
	git clone "file://$PWD" detached-orphan
'
test_expect_success 'cloned HEAD matches' '
	echo four >expect &&
	git --git-dir=detached-orphan/.git log -1 --format=%s >actual &&
	test_cmp expect actual
'
test_expect_success 'cloned HEAD is detached' '
	head_is_detached detached-orphan
'

test_done
