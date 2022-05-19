#!/bin/sh

test_description='test cloning a repository with detached HEAD'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

head_is_detached() {
	but --but-dir=$1/.but rev-parse --verify HEAD &&
	test_must_fail but --but-dir=$1/.but symbolic-ref HEAD
}

test_expect_success 'setup' '
	echo one >file &&
	but add file &&
	but cummit -m one &&
	echo two >file &&
	but cummit -a -m two &&
	but tag two &&
	echo three >file &&
	but cummit -a -m three
'

test_expect_success 'clone repo (detached HEAD points to branch)' '
	but checkout main^0 &&
	but clone "file://$PWD" detached-branch
'
test_expect_success 'cloned HEAD matches' '
	echo three >expect &&
	but --but-dir=detached-branch/.but log -1 --format=%s >actual &&
	test_cmp expect actual
'
test_expect_failure 'cloned HEAD is detached' '
	head_is_detached detached-branch
'

test_expect_success 'clone repo (detached HEAD points to tag)' '
	but checkout two^0 &&
	but clone "file://$PWD" detached-tag
'
test_expect_success 'cloned HEAD matches' '
	echo two >expect &&
	but --but-dir=detached-tag/.but log -1 --format=%s >actual &&
	test_cmp expect actual
'
test_expect_success 'cloned HEAD is detached' '
	head_is_detached detached-tag
'

test_expect_success 'clone repo (detached HEAD points to history)' '
	but checkout two^ &&
	but clone "file://$PWD" detached-history
'
test_expect_success 'cloned HEAD matches' '
	echo one >expect &&
	but --but-dir=detached-history/.but log -1 --format=%s >actual &&
	test_cmp expect actual
'
test_expect_success 'cloned HEAD is detached' '
	head_is_detached detached-history
'

test_expect_success 'clone repo (orphan detached HEAD)' '
	but checkout main^0 &&
	echo four >file &&
	but cummit -a -m four &&
	but clone "file://$PWD" detached-orphan
'
test_expect_success 'cloned HEAD matches' '
	echo four >expect &&
	but --but-dir=detached-orphan/.but log -1 --format=%s >actual &&
	test_cmp expect actual
'
test_expect_success 'cloned HEAD is detached' '
	head_is_detached detached-orphan
'

test_done
