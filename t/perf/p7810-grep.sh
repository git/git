#!/bin/sh

test_description="git-grep performance in various modes"

. ./perf-lib.sh

test_perf_large_repo
test_checkout_worktree

test_perf 'grep worktree, cheap regex' '
	git grep some_nonexistent_string || :
'
test_perf 'grep worktree, expensive regex' '
	git grep "^.* *some_nonexistent_string$" || :
'
test_perf 'grep --cached, cheap regex' '
	git grep --cached some_nonexistent_string || :
'
test_perf 'grep --cached, expensive regex' '
	git grep --cached "^.* *some_nonexistent_string$" || :
'

test_done
