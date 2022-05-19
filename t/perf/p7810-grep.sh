#!/bin/sh

test_description="but-grep performance in various modes"

. ./perf-lib.sh

test_perf_large_repo
test_checkout_worktree

test_perf 'grep worktree, cheap regex' '
	but grep some_nonexistent_string || :
'
test_perf 'grep worktree, expensive regex' '
	but grep "^.* *some_nonexistent_string$" || :
'
test_perf 'grep --cached, cheap regex' '
	but grep --cached some_nonexistent_string || :
'
test_perf 'grep --cached, expensive regex' '
	but grep --cached "^.* *some_nonexistent_string$" || :
'

test_done
