#!/bin/sh

test_description='Return value of diffs'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	echo 1 >a &&
	but add . &&
	but cummit -m first &&
	echo 2 >b &&
	but add . &&
	but cummit -a -m second &&
	mkdir -p test-outside/repo && (
		cd test-outside/repo &&
		but init &&
		echo "1 1" >a &&
		but add . &&
		but cummit -m 1
	) &&
	mkdir -p test-outside/non/but && (
		cd test-outside/non/but &&
		echo "1 1" >a &&
		echo "1 1" >matching-file &&
		echo "1 1 " >trailing-space &&
		echo "1   1" >extra-space &&
		echo "2" >never-match
	)
'

test_expect_success 'but diff-tree HEAD^ HEAD' '
	test_expect_code 1 but diff-tree --quiet HEAD^ HEAD >cnt &&
	test_line_count = 0 cnt
'
test_expect_success 'but diff-tree HEAD^ HEAD -- a' '
	test_expect_code 0 but diff-tree --quiet HEAD^ HEAD -- a >cnt &&
	test_line_count = 0 cnt
'
test_expect_success 'but diff-tree HEAD^ HEAD -- b' '
	test_expect_code 1 but diff-tree --quiet HEAD^ HEAD -- b >cnt &&
	test_line_count = 0 cnt
'
# this diff outputs one line: sha1 of the given head
test_expect_success 'echo HEAD | but diff-tree --stdin' '
	echo $(but rev-parse HEAD) |
	test_expect_code 1 but diff-tree --quiet --stdin >cnt &&
	test_line_count = 1 cnt
'
test_expect_success 'but diff-tree HEAD HEAD' '
	test_expect_code 0 but diff-tree --quiet HEAD HEAD >cnt &&
	test_line_count = 0 cnt
'
test_expect_success 'but diff-files' '
	test_expect_code 0 but diff-files --quiet >cnt &&
	test_line_count = 0 cnt
'
test_expect_success 'but diff-index --cached HEAD' '
	test_expect_code 0 but diff-index --quiet --cached HEAD >cnt &&
	test_line_count = 0 cnt
'
test_expect_success 'but diff-index --cached HEAD^' '
	test_expect_code 1 but diff-index --quiet --cached HEAD^ >cnt &&
	test_line_count = 0 cnt
'
test_expect_success 'but diff-index --cached HEAD^' '
	echo text >>b &&
	echo 3 >c &&
	but add . &&
	test_expect_code 1 but diff-index --quiet --cached HEAD^ >cnt &&
	test_line_count = 0 cnt
'
test_expect_success 'but diff-tree -Stext HEAD^ HEAD -- b' '
	but cummit -m "text in b" &&
	test_expect_code 1 but diff-tree --quiet -Stext HEAD^ HEAD -- b >cnt &&
	test_line_count = 0 cnt
'
test_expect_success 'but diff-tree -Snot-found HEAD^ HEAD -- b' '
	test_expect_code 0 but diff-tree --quiet -Snot-found HEAD^ HEAD -- b >cnt &&
	test_line_count = 0 cnt
'
test_expect_success 'but diff-files' '
	echo 3 >>c &&
	test_expect_code 1 but diff-files --quiet >cnt &&
	test_line_count = 0 cnt
'

test_expect_success 'but diff-index --cached HEAD' '
	but update-index c &&
	test_expect_code 1 but diff-index --quiet --cached HEAD >cnt &&
	test_line_count = 0 cnt
'

test_expect_success 'but diff, one file outside repo' '
	(
		cd test-outside/repo &&
		test_expect_code 0 but diff --quiet a ../non/but/matching-file &&
		test_expect_code 1 but diff --quiet a ../non/but/extra-space
	)
'

test_expect_success 'but diff, both files outside repo' '
	(
		BUT_CEILING_DIRECTORIES="$TRASH_DIRECTORY/test-outside" &&
		export BUT_CEILING_DIRECTORIES &&
		cd test-outside/non/but &&
		test_expect_code 0 but diff --quiet a matching-file &&
		test_expect_code 1 but diff --quiet a extra-space
	)
'

test_expect_success 'but diff --ignore-space-at-eol, one file outside repo' '
	(
		cd test-outside/repo &&
		test_expect_code 0 but diff --quiet --ignore-space-at-eol a ../non/but/trailing-space &&
		test_expect_code 1 but diff --quiet --ignore-space-at-eol a ../non/but/extra-space
	)
'

test_expect_success 'but diff --ignore-space-at-eol, both files outside repo' '
	(
		BUT_CEILING_DIRECTORIES="$TRASH_DIRECTORY/test-outside" &&
		export BUT_CEILING_DIRECTORIES &&
		cd test-outside/non/but &&
		test_expect_code 0 but diff --quiet --ignore-space-at-eol a trailing-space &&
		test_expect_code 1 but diff --quiet --ignore-space-at-eol a extra-space
	)
'

test_expect_success 'but diff --ignore-all-space, one file outside repo' '
	(
		cd test-outside/repo &&
		test_expect_code 0 but diff --quiet --ignore-all-space a ../non/but/trailing-space &&
		test_expect_code 0 but diff --quiet --ignore-all-space a ../non/but/extra-space &&
		test_expect_code 1 but diff --quiet --ignore-all-space a ../non/but/never-match
	)
'

test_expect_success 'but diff --ignore-all-space, both files outside repo' '
	(
		BUT_CEILING_DIRECTORIES="$TRASH_DIRECTORY/test-outside" &&
		export BUT_CEILING_DIRECTORIES &&
		cd test-outside/non/but &&
		test_expect_code 0 but diff --quiet --ignore-all-space a trailing-space &&
		test_expect_code 0 but diff --quiet --ignore-all-space a extra-space &&
		test_expect_code 1 but diff --quiet --ignore-all-space a never-match
	)
'

test_expect_success 'but diff --quiet ignores stat-change only entries' '
	test-tool chmtime +10 a &&
	echo modified >>b &&
	test_expect_code 1 but diff --quiet
'

test_expect_success 'but diff --quiet on a path that need conversion' '
	echo "crlf.txt text=auto" >.butattributes &&
	printf "Hello\r\nWorld\r\n" >crlf.txt &&
	but add .butattributes crlf.txt &&

	printf "Hello\r\nWorld\n" >crlf.txt &&
	but diff --quiet crlf.txt
'

test_done
