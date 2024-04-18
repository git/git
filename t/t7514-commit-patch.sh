#!/bin/sh

test_description='hunk edit with "commit -p -m"'
. ./test-lib.sh

test_expect_success 'setup (initial)' '
	echo line1 >file &&
	git add file &&
	git commit -m commit1
'

test_expect_success 'edit hunk "commit -p -m message"' '
	test_when_finished "rm -f editor_was_started" &&
	rm -f editor_was_started &&
	echo more >>file &&
	echo e | env GIT_EDITOR=": >editor_was_started" git commit -p -m commit2 file &&
	test -r editor_was_started
'

test_expect_success 'edit hunk "commit --dry-run -p -m message"' '
	test_when_finished "rm -f editor_was_started" &&
	rm -f editor_was_started &&
	echo more >>file &&
	echo e | env GIT_EDITOR=": >editor_was_started" git commit -p -m commit3 file &&
	test -r editor_was_started
'

test_done
