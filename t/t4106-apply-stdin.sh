#!/bin/sh

test_description='git apply --numstat - <patch'

. ./test-lib.sh

test_expect_success setup '
	echo hello >text &&
	git add text &&
	echo goodbye >text &&
	git diff >patch
'

test_expect_success 'git apply --numstat - < patch' '
	echo "1	1	text" >expect &&
	git apply --numstat - <patch >actual &&
	test_cmp expect actual
'

test_expect_success 'git apply --numstat - < patch patch' '
	for i in 1 2; do echo "1	1	text"; done >expect &&
	git apply --numstat - < patch patch >actual &&
	test_cmp expect actual
'

test_done
