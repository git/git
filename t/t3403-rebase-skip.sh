#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git rebase --merge --skip tests'

. ./test-lib.sh

# we assume the default git-am -3 --skip strategy is tested independently
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

test_expect_failure 'rebase with git am -3 (default)' '
	git rebase master
'

test_expect_success 'rebase --skip with am -3' '
	git reset --hard HEAD &&
	git rebase --skip
	'
test_expect_success 'checkout skip-merge' 'git checkout -f skip-merge'

test_expect_failure 'rebase with --merge' 'git rebase --merge master'

test_expect_success 'rebase --skip with --merge' '
	git reset --hard HEAD &&
	git rebase --skip
	'

test_expect_success 'merge and reference trees equal' \
	'test -z "`git diff-tree skip-merge skip-reference`"'

test_debug 'gitk --all & sleep 1'

test_done
