#!/bin/sh

test_description='basic work tree status reporting'

. ./test-lib.sh

test_expect_success setup '
	test_commit A &&
	test_commit B oneside added &&
	git checkout A^0 &&
	test_commit C oneside created
'

test_expect_success 'A/A conflict' '
	git checkout B^0 &&
	test_must_fail git merge C
'

test_expect_success 'Report path with conflict' '
	git diff --cached --name-status >actual &&
	echo "U	oneside" >expect &&
	test_cmp expect actual
'

test_expect_success 'Report new path with conflict' '
	git diff --cached --name-status HEAD^ >actual &&
	echo "U	oneside" >expect &&
	test_cmp expect actual
'

test_done
