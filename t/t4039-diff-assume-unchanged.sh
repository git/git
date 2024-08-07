#!/bin/sh

test_description='diff with assume-unchanged entries'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# external diff has been tested in t4020-diff-external.sh

test_expect_success 'setup' '
	echo zero > zero &&
	git add zero &&
	git commit -m zero &&
	echo one > one &&
	echo two > two &&
	blob=$(git hash-object one) &&
	git add one two &&
	git commit -m onetwo &&
	git update-index --assume-unchanged one &&
	echo borked >> one &&
	test "$(git ls-files -v one)" = "h one"
'

test_expect_success 'diff-index does not examine assume-unchanged entries' '
	git diff-index HEAD^ -- one | grep -q $blob
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
	git diff-files --find-copies-harder -- exec >actual &&
	test_must_be_empty actual
'

test_done
