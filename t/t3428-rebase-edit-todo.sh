#!/bin/sh

test_description='rebase should reread the todo file if an exec modifies it'

. ./test-lib.sh

todo=$(git rev-parse --git-dir)/rebase-merge/git-rebase-todo

test_expect_success setup '

	>file &&
	git add file &&
	test_tick &&
	git commit -m "Initial commit"
'

test_expect_success 'rebase exec modifies rebase-todo' "
        git rebase HEAD -x 'echo x touch F >>\"$todo\"' &&
        test -e F
"

test_done
