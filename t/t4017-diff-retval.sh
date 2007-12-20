#!/bin/sh

test_description='Return value of diffs'

. ./test-lib.sh

test_expect_success 'setup' '
	echo 1 >a &&
	git add . &&
	git commit -m first &&
	echo 2 >b &&
	git add . &&
	git commit -a -m second
'

test_expect_success 'git diff-tree HEAD^ HEAD' '
	git diff-tree --exit-code HEAD^ HEAD
	test $? = 1
'
test_expect_success 'git diff-tree HEAD^ HEAD -- a' '
	git diff-tree --exit-code HEAD^ HEAD -- a
	test $? = 0
'
test_expect_success 'git diff-tree HEAD^ HEAD -- b' '
	git diff-tree --exit-code HEAD^ HEAD -- b
	test $? = 1
'
test_expect_success 'echo HEAD | git diff-tree --stdin' '
	echo $(git rev-parse HEAD) | git diff-tree --exit-code --stdin
	test $? = 1
'
test_expect_success 'git diff-tree HEAD HEAD' '
	git diff-tree --exit-code HEAD HEAD
	test $? = 0
'
test_expect_success 'git diff-files' '
	git diff-files --exit-code
	test $? = 0
'
test_expect_success 'git diff-index --cached HEAD' '
	git diff-index --exit-code --cached HEAD
	test $? = 0
'
test_expect_success 'git diff-index --cached HEAD^' '
	git diff-index --exit-code --cached HEAD^
	test $? = 1
'
test_expect_success 'git diff-index --cached HEAD^' '
	echo text >>b &&
	echo 3 >c &&
	git add . && {
		git diff-index --exit-code --cached HEAD^
		test $? = 1
	}
'
test_expect_success 'git diff-tree -Stext HEAD^ HEAD -- b' '
	git commit -m "text in b" && {
		git diff-tree -p --exit-code -Stext HEAD^ HEAD -- b
		test $? = 1
	}
'
test_expect_success 'git diff-tree -Snot-found HEAD^ HEAD -- b' '
	git diff-tree -p --exit-code -Snot-found HEAD^ HEAD -- b
	test $? = 0
'
test_expect_success 'git diff-files' '
	echo 3 >>c && {
		git diff-files --exit-code
		test $? = 1
	}
'
test_expect_success 'git diff-index --cached HEAD' '
	git update-index c && {
		git diff-index --exit-code --cached HEAD
		test $? = 1
	}
'

test_expect_success '--check --exit-code returns 0 for no difference' '

	git diff --check --exit-code

'

test_expect_success '--check --exit-code returns 1 for a clean difference' '

	echo "good" > a &&
	git diff --check --exit-code
	test $? = 1

'

test_expect_success '--check --exit-code returns 3 for a dirty difference' '

	echo "bad   " >> a &&
	git diff --check --exit-code
	test $? = 3

'

test_expect_success '--check with --no-pager returns 2 for dirty difference' '

	git --no-pager diff --check
	test $? = 2

'

test_done
