#!/bin/sh

test_description='diff --exit-code with whitespace'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	mkdir a b &&
	echo >c &&
	echo >a/d &&
	echo >b/e &&
	but add . &&
	test_tick &&
	but cummit -m initial &&
	echo " " >a/d &&
	test_tick &&
	but cummit -a -m second &&
	echo "  " >a/d &&
	echo " " >b/e &&
	but add a/d
'

test_expect_success 'diff-tree --exit-code' '
	test_must_fail but diff --exit-code HEAD^ HEAD &&
	test_must_fail but diff-tree --exit-code HEAD^ HEAD
'

test_expect_success 'diff-tree -b --exit-code' '
	but diff -b --exit-code HEAD^ HEAD &&
	but diff-tree -b -p --exit-code HEAD^ HEAD &&
	but diff-tree -b --exit-code HEAD^ HEAD
'

test_expect_success 'diff-index --cached --exit-code' '
	test_must_fail but diff --cached --exit-code HEAD &&
	test_must_fail but diff-index --cached --exit-code HEAD
'

test_expect_success 'diff-index -b -p --cached --exit-code' '
	but diff -b --cached --exit-code HEAD &&
	but diff-index -b -p --cached --exit-code HEAD
'

test_expect_success 'diff-index --exit-code' '
	test_must_fail but diff --exit-code HEAD &&
	test_must_fail but diff-index --exit-code HEAD
'

test_expect_success 'diff-index -b -p --exit-code' '
	but diff -b --exit-code HEAD &&
	but diff-index -b -p --exit-code HEAD
'

test_expect_success 'diff-files --exit-code' '
	test_must_fail but diff --exit-code &&
	test_must_fail but diff-files --exit-code
'

test_expect_success 'diff-files -b -p --exit-code' '
	but diff -b --exit-code &&
	but diff-files -b -p --exit-code
'

test_expect_success 'diff-files --diff-filter --quiet' '
	but reset --hard &&
	rm a/d &&
	echo x >>b/e &&
	test_must_fail but diff-files --diff-filter=M --quiet
'

test_expect_success 'diff-tree --diff-filter --quiet' '
	but cummit -a -m "worktree state" &&
	test_must_fail but diff-tree --diff-filter=M --quiet HEAD^ HEAD
'

test_done
