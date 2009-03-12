#!/bin/sh

test_description='test multi-tree read-tree without merging'

. ./test-lib.sh

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
	git read-tree initial master side &&
	(echo a; echo b/c) >expect &&
	git ls-files >actual &&
	test_cmp expect actual
'

test_done

