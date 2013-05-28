#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git rebase --merge --skip tests'

. ./test-lib.sh

# we assume the default git am -3 --skip strategy is tested independently
# and always works :)

test_expect_success setup '
	echo hello > hello &&
	git add hello &&
	git commit -m "hello" &&
	git branch skip-reference &&

	echo world >> hello &&
	git commit -a -m "hello world" &&
	echo goodbye >> hello &&
	git commit -a -m "goodbye" &&

	git checkout -f skip-reference &&
	echo moo > hello &&
	git commit -a -m "we should skip this" &&
	echo moo > cow &&
	git add cow &&
	git commit -m "this should not be skipped" &&
	git branch pre-rebase skip-reference &&
	git branch skip-merge skip-reference
	'

test_expect_success 'rebase with git am -3 (default)' '
	test_must_fail git rebase master
'

test_expect_success 'rebase --skip can not be used with other options' '
	test_must_fail git rebase -v --skip &&
	test_must_fail git rebase --skip -v
'

test_expect_success 'rebase --skip with am -3' '
	git rebase --skip
	'

test_expect_success 'rebase moves back to skip-reference' '
	test refs/heads/skip-reference = $(git symbolic-ref HEAD) &&
	git branch post-rebase &&
	git reset --hard pre-rebase &&
	test_must_fail git rebase master &&
	echo "hello" > hello &&
	git add hello &&
	git rebase --continue &&
	test refs/heads/skip-reference = $(git symbolic-ref HEAD) &&
	git reset --hard post-rebase
'

test_expect_success 'checkout skip-merge' 'git checkout -f skip-merge'

test_expect_success 'rebase with --merge' '
	test_must_fail git rebase --merge master
'

test_expect_success 'rebase --skip with --merge' '
	git rebase --skip
'

test_expect_success 'merge and reference trees equal' '
	test -z "`git diff-tree skip-merge skip-reference`"
'

test_expect_success 'moved back to branch correctly' '
	test refs/heads/skip-merge = $(git symbolic-ref HEAD)
'

test_debug 'gitk --all & sleep 1'

test_done
