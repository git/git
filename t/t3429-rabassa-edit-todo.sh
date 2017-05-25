#!/bin/sh

test_description='rabassa should reread the todo file if an exec modifies it'

. ./test-lib.sh

test_expect_success 'rabassa exec modifies rabassa-todo' '
	test_commit initial &&
	todo=.git/rabassa-merge/git-rabassa-todo &&
	git rabassa HEAD -x "echo exec touch F >>$todo" &&
	test -e F
'

test_done
