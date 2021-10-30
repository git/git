#!/bin/sh

test_description='diff --exit-code with whitespace'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	mkdir a b &&
	echo >c &&
	echo >a/d &&
	echo >b/e &&
	git add . &&
	test_tick &&
	git commit -m initial &&
	echo " " >a/d &&
	test_tick &&
	git commit -a -m second &&
	echo "  " >a/d &&
	echo " " >b/e &&
	git add a/d
'

test_expect_success 'diff-tree --exit-code' '
	test_must_fail git diff --exit-code HEAD^ HEAD &&
	test_must_fail git diff-tree --exit-code HEAD^ HEAD
'

test_expect_success 'diff-tree -b --exit-code' '
	git diff -b --exit-code HEAD^ HEAD &&
	git diff-tree -b -p --exit-code HEAD^ HEAD &&
	git diff-tree -b --exit-code HEAD^ HEAD
'

test_expect_success 'diff-index --cached --exit-code' '
	test_must_fail git diff --cached --exit-code HEAD &&
	test_must_fail git diff-index --cached --exit-code HEAD
'

test_expect_success 'diff-index -b -p --cached --exit-code' '
	git diff -b --cached --exit-code HEAD &&
	git diff-index -b -p --cached --exit-code HEAD
'

test_expect_success 'diff-index --exit-code' '
	test_must_fail git diff --exit-code HEAD &&
	test_must_fail git diff-index --exit-code HEAD
'

test_expect_success 'diff-index -b -p --exit-code' '
	git diff -b --exit-code HEAD &&
	git diff-index -b -p --exit-code HEAD
'

test_expect_success 'diff-files --exit-code' '
	test_must_fail git diff --exit-code &&
	test_must_fail git diff-files --exit-code
'

test_expect_success 'diff-files -b -p --exit-code' '
	git diff -b --exit-code &&
	git diff-files -b -p --exit-code
'

test_expect_success 'diff-files --diff-filter --quiet' '
	git reset --hard &&
	rm a/d &&
	echo x >>b/e &&
	test_must_fail git diff-files --diff-filter=M --quiet
'

test_expect_success 'diff-tree --diff-filter --quiet' '
	git commit -a -m "worktree state" &&
	test_must_fail git diff-tree --diff-filter=M --quiet HEAD^ HEAD
'

test_done
