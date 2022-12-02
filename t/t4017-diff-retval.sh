#!/bin/sh

test_description='Return value of diffs'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	echo "1 " >a &&
	git add . &&
	git commit -m zeroth &&
	echo 1 >a &&
	git add . &&
	git commit -m first &&
	echo 2 >b &&
	git add . &&
	git commit -a -m second
'

test_expect_success 'git diff --quiet -w  HEAD^^ HEAD^' '
	git diff --quiet -w HEAD^^ HEAD^
'

test_expect_success 'git diff --quiet HEAD^^ HEAD^' '
	test_must_fail git diff --quiet HEAD^^ HEAD^
'

test_expect_success 'git diff --quiet -w  HEAD^ HEAD' '
	test_must_fail git diff --quiet -w HEAD^ HEAD
'

test_expect_success 'git diff-tree HEAD^ HEAD' '
	test_expect_code 1 git diff-tree --exit-code HEAD^ HEAD
'
test_expect_success 'git diff-tree HEAD^ HEAD -- a' '
	git diff-tree --exit-code HEAD^ HEAD -- a
'
test_expect_success 'git diff-tree HEAD^ HEAD -- b' '
	test_expect_code 1 git diff-tree --exit-code HEAD^ HEAD -- b
'
test_expect_success 'echo HEAD | git diff-tree --stdin' '
	echo $(git rev-parse HEAD) | test_expect_code 1 git diff-tree --exit-code --stdin
'
test_expect_success 'git diff-tree HEAD HEAD' '
	git diff-tree --exit-code HEAD HEAD
'
test_expect_success 'git diff-files' '
	git diff-files --exit-code
'
test_expect_success 'git diff-index --cached HEAD' '
	git diff-index --exit-code --cached HEAD
'
test_expect_success 'git diff-index --cached HEAD^' '
	test_expect_code 1 git diff-index --exit-code --cached HEAD^
'
test_expect_success 'git diff-index --cached HEAD^' '
	echo text >>b &&
	echo 3 >c &&
	git add . &&
	test_expect_code 1 git diff-index --exit-code --cached HEAD^
'
test_expect_success 'git diff-tree -Stext HEAD^ HEAD -- b' '
	git commit -m "text in b" &&
	test_expect_code 1 git diff-tree -p --exit-code -Stext HEAD^ HEAD -- b
'
test_expect_success 'git diff-tree -Snot-found HEAD^ HEAD -- b' '
	git diff-tree -p --exit-code -Snot-found HEAD^ HEAD -- b
'
test_expect_success 'git diff-files' '
	echo 3 >>c &&
	test_expect_code 1 git diff-files --exit-code
'
test_expect_success 'git diff-index --cached HEAD' '
	git update-index c &&
	test_expect_code 1 git diff-index --exit-code --cached HEAD
'

test_expect_success '--check --exit-code returns 0 for no difference' '

	git diff --check --exit-code

'

test_expect_success '--check --exit-code returns 1 for a clean difference' '

	echo "good" > a &&
	test_expect_code 1 git diff --check --exit-code

'

test_expect_success '--check --exit-code returns 3 for a dirty difference' '

	echo "bad   " >> a &&
	test_expect_code 3 git diff --check --exit-code

'

test_expect_success '--check with --no-pager returns 2 for dirty difference' '

	test_expect_code 2 git --no-pager diff --check

'

test_expect_success 'check should test not just the last line' '
	echo "" >>a &&
	test_expect_code 2 git --no-pager diff --check

'

test_expect_success 'check detects leftover conflict markers' '
	git reset --hard &&
	git checkout HEAD^ &&
	echo binary >>b &&
	git commit -m "side" b &&
	test_must_fail git merge main &&
	git add b &&
	test_expect_code 2 git --no-pager diff --cached --check >test.out &&
	test 3 = $(grep "conflict marker" test.out | wc -l) &&
	git reset --hard
'

test_expect_success 'check honors conflict marker length' '
	git reset --hard &&
	echo ">>>>>>> boo" >>b &&
	echo "======" >>a &&
	git diff --check a &&
	test_expect_code 2 git diff --check b &&
	git reset --hard &&
	echo ">>>>>>>> boo" >>b &&
	echo "========" >>a &&
	git diff --check &&
	echo "b conflict-marker-size=8" >.gitattributes &&
	test_expect_code 2 git diff --check b &&
	git diff --check a &&
	git reset --hard
'

test_done
