#!/bin/sh

test_description='test git worktree repair'

. ./test-lib.sh

test_expect_success setup '
	test_commit init
'

test_done
