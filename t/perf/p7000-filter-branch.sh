#!/bin/sh

test_description='performance of filter-branch'
. ./perf-lib.sh

test_perf_default_repo
test_checkout_worktree

test_expect_success 'mark bases for tests' '
	git tag -f tip &&
	git tag -f base HEAD~100
'

test_perf 'noop filter' '
	git checkout --detach tip &&
	git filter-branch -f base..HEAD
'

test_perf 'noop prune-empty' '
	git checkout --detach tip &&
	git filter-branch -f --prune-empty base..HEAD
'

test_done
