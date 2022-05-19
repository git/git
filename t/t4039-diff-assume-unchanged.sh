#!/bin/sh

test_description='diff with assume-unchanged entries'

. ./test-lib.sh

# external diff has been tested in t4020-diff-external.sh

test_expect_success 'setup' '
	echo zero > zero &&
	but add zero &&
	but cummit -m zero &&
	echo one > one &&
	echo two > two &&
	blob=$(but hash-object one) &&
	but add one two &&
	but cummit -m onetwo &&
	but update-index --assume-unchanged one &&
	echo borked >> one &&
	test "$(but ls-files -v one)" = "h one"
'

test_expect_success 'diff-index does not examine assume-unchanged entries' '
	but diff-index HEAD^ -- one | grep -q $blob
'

test_expect_success 'diff-files does not examine assume-unchanged entries' '
	rm one &&
	test -z "$(but diff-files -- one)"
'

test_expect_success POSIXPERM 'find-copies-harder is not confused by mode bits' '
	echo content >exec &&
	chmod +x exec &&
	but add exec &&
	but cummit -m exec &&
	but update-index --assume-unchanged exec &&
	but diff-files --find-copies-harder -- exec >actual &&
	test_must_be_empty actual
'

test_done
