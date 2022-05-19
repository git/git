#!/bin/sh

test_description='test multi-tree read-tree without merging'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-read-tree.sh

test_expect_success setup '
	echo one >a &&
	but add a &&
	but cummit -m initial &&
	but tag initial &&
	echo two >b &&
	but add b &&
	but cummit -m second &&
	but checkout -b side initial &&
	echo three >a &&
	mkdir b &&
	echo four >b/c &&
	but add b/c &&
	but cummit -m third
'

test_expect_success 'multi-read' '
	read_tree_must_succeed initial main side &&
	test_write_lines a b/c >expect &&
	but ls-files >actual &&
	test_cmp expect actual
'

test_done

