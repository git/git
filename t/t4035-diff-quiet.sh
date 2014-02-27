#!/bin/sh

test_description='Return value of diffs'

. ./test-lib.sh

test_expect_success 'setup' '
	echo 1 >a &&
	git add . &&
	git commit -m first &&
	echo 2 >b &&
	git add . &&
	git commit -a -m second &&
	mkdir -p test-outside/repo && (
		cd test-outside/repo &&
		git init &&
		echo "1 1" >a &&
		git add . &&
		git commit -m 1
	) &&
	mkdir -p test-outside/non/git && (
		cd test-outside/non/git &&
		echo "1 1" >a &&
		echo "1 1" >matching-file &&
		echo "1 1 " >trailing-space &&
		echo "1   1" >extra-space &&
		echo "2" >never-match
	)
'

test_expect_success 'git diff-tree HEAD^ HEAD' '
	git diff-tree --quiet HEAD^ HEAD >cnt
	test $? = 1 && test_line_count = 0 cnt
'
test_expect_success 'git diff-tree HEAD^ HEAD -- a' '
	git diff-tree --quiet HEAD^ HEAD -- a >cnt
	test $? = 0 && test_line_count = 0 cnt
'
test_expect_success 'git diff-tree HEAD^ HEAD -- b' '
	git diff-tree --quiet HEAD^ HEAD -- b >cnt
	test $? = 1 && test_line_count = 0 cnt
'
# this diff outputs one line: sha1 of the given head
test_expect_success 'echo HEAD | git diff-tree --stdin' '
	echo $(git rev-parse HEAD) | git diff-tree --quiet --stdin >cnt
	test $? = 1 && test_line_count = 1 cnt
'
test_expect_success 'git diff-tree HEAD HEAD' '
	git diff-tree --quiet HEAD HEAD >cnt
	test $? = 0 && test_line_count = 0 cnt
'
test_expect_success 'git diff-files' '
	git diff-files --quiet >cnt
	test $? = 0 && test_line_count = 0 cnt
'
test_expect_success 'git diff-index --cached HEAD' '
	git diff-index --quiet --cached HEAD >cnt
	test $? = 0 && test_line_count = 0 cnt
'
test_expect_success 'git diff-index --cached HEAD^' '
	git diff-index --quiet --cached HEAD^ >cnt
	test $? = 1 && test_line_count = 0 cnt
'
test_expect_success 'git diff-index --cached HEAD^' '
	echo text >>b &&
	echo 3 >c &&
	git add . && {
		git diff-index --quiet --cached HEAD^ >cnt
		test $? = 1 && test_line_count = 0 cnt
	}
'
test_expect_success 'git diff-tree -Stext HEAD^ HEAD -- b' '
	git commit -m "text in b" && {
		git diff-tree --quiet -Stext HEAD^ HEAD -- b >cnt
		test $? = 1 && test_line_count = 0 cnt
	}
'
test_expect_success 'git diff-tree -Snot-found HEAD^ HEAD -- b' '
	git diff-tree --quiet -Snot-found HEAD^ HEAD -- b >cnt
	test $? = 0 && test_line_count = 0 cnt
'
test_expect_success 'git diff-files' '
	echo 3 >>c && {
		git diff-files --quiet >cnt
		test $? = 1 && test_line_count = 0 cnt
	}
'
test_expect_success 'git diff-index --cached HEAD' '
	git update-index c && {
		git diff-index --quiet --cached HEAD >cnt
		test $? = 1 && test_line_count = 0 cnt
	}
'

test_expect_success 'git diff, one file outside repo' '
	(
		cd test-outside/repo &&
		test_expect_code 0 git diff --quiet a ../non/git/matching-file &&
		test_expect_code 1 git diff --quiet a ../non/git/extra-space
	)
'

test_expect_success 'git diff, both files outside repo' '
	(
		GIT_CEILING_DIRECTORIES="$TRASH_DIRECTORY/test-outside" &&
		export GIT_CEILING_DIRECTORIES &&
		cd test-outside/non/git &&
		test_expect_code 0 git diff --quiet a matching-file &&
		test_expect_code 1 git diff --quiet a extra-space
	)
'

test_expect_success 'git diff --ignore-space-at-eol, one file outside repo' '
	(
		cd test-outside/repo &&
		test_expect_code 0 git diff --quiet --ignore-space-at-eol a ../non/git/trailing-space &&
		test_expect_code 1 git diff --quiet --ignore-space-at-eol a ../non/git/extra-space
	)
'

test_expect_success 'git diff --ignore-space-at-eol, both files outside repo' '
	(
		GIT_CEILING_DIRECTORIES="$TRASH_DIRECTORY/test-outside" &&
		export GIT_CEILING_DIRECTORIES &&
		cd test-outside/non/git &&
		test_expect_code 0 git diff --quiet --ignore-space-at-eol a trailing-space &&
		test_expect_code 1 git diff --quiet --ignore-space-at-eol a extra-space
	)
'

test_expect_success 'git diff --ignore-all-space, one file outside repo' '
	(
		cd test-outside/repo &&
		test_expect_code 0 git diff --quiet --ignore-all-space a ../non/git/trailing-space &&
		test_expect_code 0 git diff --quiet --ignore-all-space a ../non/git/extra-space &&
		test_expect_code 1 git diff --quiet --ignore-all-space a ../non/git/never-match
	)
'

test_expect_success 'git diff --ignore-all-space, both files outside repo' '
	(
		GIT_CEILING_DIRECTORIES="$TRASH_DIRECTORY/test-outside" &&
		export GIT_CEILING_DIRECTORIES &&
		cd test-outside/non/git &&
		test_expect_code 0 git diff --quiet --ignore-all-space a trailing-space &&
		test_expect_code 0 git diff --quiet --ignore-all-space a extra-space &&
		test_expect_code 1 git diff --quiet --ignore-all-space a never-match
	)
'

test_expect_success 'git diff --quiet ignores stat-change only entries' '
	test-chmtime +10 a &&
	echo modified >>b &&
	test_expect_code 1 git diff --quiet
'

test_done
