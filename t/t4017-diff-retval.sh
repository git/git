#!/bin/sh

test_description='Return value of diffs'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	echo "1 " >a &&
	but add . &&
	but cummit -m zeroth &&
	echo 1 >a &&
	but add . &&
	but cummit -m first &&
	echo 2 >b &&
	but add . &&
	but cummit -a -m second
'

test_expect_success 'but diff --quiet -w  HEAD^^ HEAD^' '
	but diff --quiet -w HEAD^^ HEAD^
'

test_expect_success 'but diff --quiet HEAD^^ HEAD^' '
	test_must_fail but diff --quiet HEAD^^ HEAD^
'

test_expect_success 'but diff --quiet -w  HEAD^ HEAD' '
	test_must_fail but diff --quiet -w HEAD^ HEAD
'

test_expect_success 'but diff-tree HEAD^ HEAD' '
	test_expect_code 1 but diff-tree --exit-code HEAD^ HEAD
'
test_expect_success 'but diff-tree HEAD^ HEAD -- a' '
	but diff-tree --exit-code HEAD^ HEAD -- a
'
test_expect_success 'but diff-tree HEAD^ HEAD -- b' '
	test_expect_code 1 but diff-tree --exit-code HEAD^ HEAD -- b
'
test_expect_success 'echo HEAD | but diff-tree --stdin' '
	echo $(but rev-parse HEAD) | test_expect_code 1 but diff-tree --exit-code --stdin
'
test_expect_success 'but diff-tree HEAD HEAD' '
	but diff-tree --exit-code HEAD HEAD
'
test_expect_success 'but diff-files' '
	but diff-files --exit-code
'
test_expect_success 'but diff-index --cached HEAD' '
	but diff-index --exit-code --cached HEAD
'
test_expect_success 'but diff-index --cached HEAD^' '
	test_expect_code 1 but diff-index --exit-code --cached HEAD^
'
test_expect_success 'but diff-index --cached HEAD^' '
	echo text >>b &&
	echo 3 >c &&
	but add . &&
	test_expect_code 1 but diff-index --exit-code --cached HEAD^
'
test_expect_success 'but diff-tree -Stext HEAD^ HEAD -- b' '
	but cummit -m "text in b" &&
	test_expect_code 1 but diff-tree -p --exit-code -Stext HEAD^ HEAD -- b
'
test_expect_success 'but diff-tree -Snot-found HEAD^ HEAD -- b' '
	but diff-tree -p --exit-code -Snot-found HEAD^ HEAD -- b
'
test_expect_success 'but diff-files' '
	echo 3 >>c &&
	test_expect_code 1 but diff-files --exit-code
'
test_expect_success 'but diff-index --cached HEAD' '
	but update-index c &&
	test_expect_code 1 but diff-index --exit-code --cached HEAD
'

test_expect_success '--check --exit-code returns 0 for no difference' '

	but diff --check --exit-code

'

test_expect_success '--check --exit-code returns 1 for a clean difference' '

	echo "good" > a &&
	test_expect_code 1 but diff --check --exit-code

'

test_expect_success '--check --exit-code returns 3 for a dirty difference' '

	echo "bad   " >> a &&
	test_expect_code 3 but diff --check --exit-code

'

test_expect_success '--check with --no-pager returns 2 for dirty difference' '

	test_expect_code 2 but --no-pager diff --check

'

test_expect_success 'check should test not just the last line' '
	echo "" >>a &&
	test_expect_code 2 but --no-pager diff --check

'

test_expect_success 'check detects leftover conflict markers' '
	but reset --hard &&
	but checkout HEAD^ &&
	echo binary >>b &&
	but cummit -m "side" b &&
	test_must_fail but merge main &&
	but add b &&
	test_expect_code 2 but --no-pager diff --cached --check >test.out &&
	test 3 = $(grep "conflict marker" test.out | wc -l) &&
	but reset --hard
'

test_expect_success 'check honors conflict marker length' '
	but reset --hard &&
	echo ">>>>>>> boo" >>b &&
	echo "======" >>a &&
	but diff --check a &&
	test_expect_code 2 but diff --check b &&
	but reset --hard &&
	echo ">>>>>>>> boo" >>b &&
	echo "========" >>a &&
	but diff --check &&
	echo "b conflict-marker-size=8" >.butattributes &&
	test_expect_code 2 but diff --check b &&
	but diff --check a &&
	but reset --hard
'

test_done
