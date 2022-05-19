#!/bin/sh

test_description='performance of filter-branch'
. ./perf-lib.sh

test_perf_default_repo
test_checkout_worktree

test_expect_success 'mark bases for tests' '
	but tag -f tip &&
	but tag -f base HEAD~100
'

test_perf 'noop filter' '
	but checkout --detach tip &&
	but filter-branch -f base..HEAD
'

test_perf 'noop prune-empty' '
	but checkout --detach tip &&
	but filter-branch -f --prune-empty base..HEAD
'

test_done
