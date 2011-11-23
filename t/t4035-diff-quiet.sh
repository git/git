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
	git diff-tree --quiet HEAD^ HEAD >cnt
	test $? = 1 && test $(wc -l <cnt) = 0
'
test_expect_success 'git diff-tree HEAD^ HEAD -- a' '
	git diff-tree --quiet HEAD^ HEAD -- a >cnt
	test $? = 0 && test $(wc -l <cnt) = 0
'
test_expect_success 'git diff-tree HEAD^ HEAD -- b' '
	git diff-tree --quiet HEAD^ HEAD -- b >cnt
	test $? = 1 && test $(wc -l <cnt) = 0
'
# this diff outputs one line: sha1 of the given head
test_expect_success 'echo HEAD | git diff-tree --stdin' '
	echo $(git rev-parse HEAD) | git diff-tree --quiet --stdin >cnt
	test $? = 1 && test $(wc -l <cnt) = 1
'
test_expect_success 'git diff-tree HEAD HEAD' '
	git diff-tree --quiet HEAD HEAD >cnt
	test $? = 0 && test $(wc -l <cnt) = 0
'
test_expect_success 'git diff-files' '
	git diff-files --quiet >cnt
	test $? = 0 && test $(wc -l <cnt) = 0
'
test_expect_success 'git diff-index --cached HEAD' '
	git diff-index --quiet --cached HEAD >cnt
	test $? = 0 && test $(wc -l <cnt) = 0
'
test_expect_success 'git diff-index --cached HEAD^' '
	git diff-index --quiet --cached HEAD^ >cnt
	test $? = 1 && test $(wc -l <cnt) = 0
'
test_expect_success 'git diff-index --cached HEAD^' '
	echo text >>b &&
	echo 3 >c &&
	git add . && {
		git diff-index --quiet --cached HEAD^ >cnt
		test $? = 1 && test $(wc -l <cnt) = 0
	}
'
test_expect_success 'git diff-tree -Stext HEAD^ HEAD -- b' '
	git commit -m "text in b" && {
		git diff-tree --quiet -Stext HEAD^ HEAD -- b >cnt
		test $? = 1 && test $(wc -l <cnt) = 0
	}
'
test_expect_success 'git diff-tree -Snot-found HEAD^ HEAD -- b' '
	git diff-tree --quiet -Snot-found HEAD^ HEAD -- b >cnt
	test $? = 0 && test $(wc -l <cnt) = 0
'
test_expect_success 'git diff-files' '
	echo 3 >>c && {
		git diff-files --quiet >cnt
		test $? = 1 && test $(wc -l <cnt) = 0
	}
'
test_expect_success 'git diff-index --cached HEAD' '
	git update-index c && {
		git diff-index --quiet --cached HEAD >cnt
		test $? = 1 && test $(wc -l <cnt) = 0
	}
'

test_done
