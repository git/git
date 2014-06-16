#!/bin/sh

test_description='diff with assume-unchanged entries'

. ./test-lib.sh

# external diff has been tested in t4020-diff-external.sh

test_expect_success 'setup' '
	echo zero > zero &&
	git add zero &&
	git commit -m zero &&
	echo one > one &&
	echo two > two &&
	git add one two &&
	git commit -m onetwo &&
	git update-index --assume-unchanged one &&
	echo borked >> one &&
	test "$(git ls-files -v one)" = "h one"
'

test_expect_success 'diff-index does not examine assume-unchanged entries' '
	git diff-index HEAD^ -- one | grep -q 5626abf0f72e58d7a153368ba57db4c673c0e171
'

test_expect_success 'diff-files does not examine assume-unchanged entries' '
	rm one &&
	test -z "$(git diff-files -- one)"
'

test_expect_success POSIXPERM 'find-copies-harder is not confused by mode bits' '
	echo content >exec &&
	chmod +x exec &&
	git add exec &&
	git commit -m exec &&
	git update-index --assume-unchanged exec &&
	>expect &&
	git diff-files --find-copies-harder -- exec >actual &&
	test_cmp expect actual
'

test_done
