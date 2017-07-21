#!/bin/sh

test_description='rebase should reread the todo file if an exec modifies it'

. ./test-lib.sh

test_expect_success 'rebase exec modifies rebase-todo' '
	test_commit initial &&
	todo=.git/rebase-merge/git-rebase-todo &&
	git rebase HEAD -x "echo exec touch F >>$todo" &&
	test -e F
'

test_done
