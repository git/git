#!/bin/sh

test_description='test multi-tree read-tree without merging'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-read-tree.sh

test_expect_success setup '
	echo one >a &&
	git add a &&
	git commit -m initial &&
	git tag initial &&
	echo two >b &&
	git add b &&
	git commit -m second &&
	git checkout -b side initial &&
	echo three >a &&
	mkdir b &&
	echo four >b/c &&
	git add b/c &&
	git commit -m third
'

test_expect_success 'multi-read' '
	read_tree_must_succeed initial main side &&
	test_write_lines a b/c >expect &&
	git ls-files >actual &&
	test_cmp expect actual
'

test_done

